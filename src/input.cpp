#include "ee538/input.hpp"
#include "ee538/graph.hpp"
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
namespace ee538 {
namespace {
// JSON floats and numeric strings are rejected even when convertible to integers.
std::int64_t integer(const nlohmann::json& j) {
    if (!j.is_number_integer()) throw std::invalid_argument("expected JSON integer");
    if (j.is_number_unsigned() && j.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        throw std::invalid_argument("integer out of range");
    return j.get<std::int64_t>();
}
constexpr std::int64_t bound = 1000000000000LL;
void positive(std::int64_t n, const char* field) {
    if (n <= 0 || n > bound) throw std::invalid_argument(std::string(field) + " must be in [1, 10^12]");
}
void nonnegative(std::int64_t n, const char* field) {
    if (n < 0 || n > bound) throw std::invalid_argument(std::string(field) + " must be in [0, 10^12]");
}
}
const char* state_name(State s) {
    switch (s) {
    case State::NotArrived: return "not_arrived";
    case State::Waiting: return "waiting";
    case State::Admitted: return "admitted";
    case State::Prefilling: return "prefilling";
    case State::Decoding: return "decoding";
    case State::Completed: return "completed";
    case State::Cancelled: return "cancelled";
    }
    throw std::logic_error("unknown request state");
}
Workload parse_workload(const nlohmann::json& input) {
    Workload w;
    if (integer(input.at("schema_version")) != 1) throw std::invalid_argument("unsupported schema_version");
    const auto& resources = input.at("resources");
    w.resources.kv_capacity_tokens = integer(resources.at("kv_capacity_tokens"));
    w.resources.max_batch_tokens = integer(resources.at("max_batch_tokens"));
    if (!resources.at("chunk_sizes").is_array()) throw std::invalid_argument("chunk_sizes must be an array");
    for (const auto& chunk : resources.at("chunk_sizes")) w.resources.chunk_sizes.push_back(integer(chunk));
    w.deadline_us = integer(input.at("deadline_us"));
    const auto seed = integer(input.at("seed"));
    if (seed < 0) throw std::invalid_argument("negative seed");
    w.seed = static_cast<std::uint64_t>(seed);
    if (input.contains("aging_us")) w.aging_us = integer(input.at("aging_us"));
    if (input.contains("history_capacity")) {
        const auto n = integer(input.at("history_capacity"));
        if (n <= 0 || n > 100000) throw std::invalid_argument("invalid history_capacity");
        w.history_capacity = static_cast<std::size_t>(n);
    }
    if (input.contains("background_w")) {
        if (!input.at("background_w").is_number()) throw std::invalid_argument("background_w must be numeric");
        w.background_w = input.at("background_w").get<double>();
    }
    const auto& requests = input.at("requests");
    if (!requests.is_array() || requests.size() > 100000) throw std::invalid_argument("requests must be array of <=100000 entries");
    std::map<std::string, RequestId> ids;
    for (const auto& item : requests) {
        auto id = item.at("id").get<std::string>();
        if (id.empty() || !ids.emplace(id, ids.size()).second) throw std::invalid_argument("empty or duplicate request ID");
    }
    for (const auto& item : requests) {
        RequestInput r;
        r.spec.id = item.at("id").get<std::string>();
        r.spec.agent = item.at("agent").get<std::string>();
        r.spec.arrival_us = integer(item.at("arrival_us"));
        r.spec.prompt_tokens = integer(item.at("prompt_tokens"));
        r.spec.max_output_tokens = integer(item.at("max_output_tokens"));
        r.actual_output_tokens = integer(item.at("actual_output_tokens"));
        if (!item.at("dependencies").is_array()) throw std::invalid_argument("dependencies must be array");
        for (const auto& dep : item.at("dependencies")) {
            const auto it = ids.find(dep.get<std::string>());
            if (it == ids.end()) throw std::invalid_argument("unknown dependency ID");
            r.spec.dependencies.push_back(it->second);
        }
        if (item.contains("cancel_at_us") && !item.at("cancel_at_us").is_null()) r.cancel_at_us = integer(item.at("cancel_at_us"));
        w.requests.push_back(std::move(r));
    }
    validate_workload(w);
    return w;
}
void validate_workload(const Workload& w) {
    positive(w.resources.kv_capacity_tokens, "kv_capacity_tokens");
    positive(w.resources.max_batch_tokens, "max_batch_tokens");
    positive(w.aging_us, "aging_us");
    nonnegative(w.deadline_us, "deadline_us");
    if (w.history_capacity == 0 || w.history_capacity > 100000) throw std::invalid_argument("invalid history_capacity");
    if (!std::isfinite(w.background_w) || w.background_w < 0 || w.background_w > 1e9) throw std::invalid_argument("invalid background power");
    if (w.resources.chunk_sizes.empty() || w.resources.chunk_sizes.size() > 64) throw std::invalid_argument("need 1..64 chunk sizes");
    std::set<Tokens> chunks;
    for (const auto chunk : w.resources.chunk_sizes) {
        positive(chunk, "chunk size");
        if (chunk > w.resources.max_batch_tokens || !chunks.insert(chunk).second) throw std::invalid_argument("duplicate chunk or chunk exceeds token budget");
    }
    if (w.requests.size() > 100000) throw std::invalid_argument("too many requests");
    std::set<std::string> ids;
    for (const auto& r : w.requests) {
        if (r.spec.id.empty() || r.spec.agent.empty() || !ids.insert(r.spec.id).second) throw std::invalid_argument("invalid ID or agent");
        nonnegative(r.spec.arrival_us, "arrival_us");
        positive(r.spec.prompt_tokens, "prompt_tokens");
        positive(r.spec.max_output_tokens, "max_output_tokens");
        nonnegative(r.actual_output_tokens, "actual_output_tokens");
        if (r.actual_output_tokens > r.spec.max_output_tokens) throw std::invalid_argument("actual output exceeds maximum");
        if (r.spec.prompt_tokens > w.resources.kv_capacity_tokens - r.spec.max_output_tokens)
            throw std::invalid_argument("request reservation exceeds KV capacity");
        if (r.cancel_at_us) {
            nonnegative(*r.cancel_at_us, "cancel_at_us");
            if (*r.cancel_at_us < r.spec.arrival_us) throw std::invalid_argument("cancellation precedes arrival");
        }
    }
    (void)TaskGraph(w.requests).topological_order();
}
} // namespace ee538
