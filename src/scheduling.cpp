#include "ee538/scheduling.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
namespace ee538 {
namespace {
bool runnable(State s) { return s == State::Admitted || s == State::Prefilling || s == State::Decoding; }
const RequestView& lookup(const Snapshot& s, RequestId id) {
    for (const auto& r : s.arrived) if (r.key == id) return r;
    throw std::logic_error("plan references invisible request");
}
std::vector<const RequestView*> ordered(const Snapshot& s) {
    std::vector<const RequestView*> list;
    for (const auto& r : s.arrived) if (runnable(r.runtime.state)) list.push_back(&r);
    std::sort(list.begin(), list.end(), [](const auto* a, const auto* b) {
        return std::tie(a->spec.arrival_us, a->key) < std::tie(b->spec.arrival_us, b->key);
    });
    return list;
}
BatchItem next_item(const RequestView& r, Tokens chunk, Tokens budget) {
    if (r.runtime.prefilled < r.spec.prompt_tokens)
        return {r.key, Phase::Prefill, std::min({chunk, budget, r.spec.prompt_tokens - r.runtime.prefilled})};
    return {r.key, Phase::Decode, 1};
}
void valid_cost(const Cost& c) {
    if (c.duration_us <= 0 || !std::isfinite(c.dynamic_j) || c.dynamic_j < 0)
        throw std::runtime_error("invalid predicted cost");
}
class GreedyPolicy final : public ISchedulingPolicy {
    std::string name_;
    Time aging_;
    BatchBuilder builder_;
public:
    GreedyPolicy(std::string name, Time aging) : name_(std::move(name)), aging_(aging) {}
    const char* name() const override { return name_.c_str(); }
    std::vector<RequestId> admission_order(const Snapshot& s) const override {
        std::vector<const RequestView*> list;
        for (const auto& r : s.arrived) if (r.runtime.state == State::Waiting) list.push_back(&r);
        std::sort(list.begin(), list.end(), [&](const auto* a, const auto* b) {
            const bool a_old = s.now_us - a->spec.arrival_us >= aging_;
            const bool b_old = s.now_us - b->spec.arrival_us >= aging_;
            if (a_old != b_old) return a_old;
            // FCFS remains strict. Optimized policies favor small reservations until aging applies.
            if (name_ != "fcfs" && !a_old) {
                const Tokens a_size = a->spec.prompt_tokens + a->spec.max_output_tokens;
                const Tokens b_size = b->spec.prompt_tokens + b->spec.max_output_tokens;
                if (a_size != b_size) return a_size < b_size;
            }
            return std::tie(a->spec.arrival_us, a->key) < std::tie(b->spec.arrival_us, b->key);
        });
        std::vector<RequestId> ids;
        for (const auto* r : list) ids.push_back(r->key);
        return ids;
    }
    BatchPlan choose(const Snapshot& s, const ICostPredictor& predictor) const override {
        if (name_ == "fcfs") {
            auto plan = builder_.build(s, s.resources.chunk_sizes.front(), false);
            plan.reason = "fixed_chunk_fcfs";
            return plan;
        }
        const auto list = ordered(s);
        // Aged active requests receive a singleton batch. No intentional idling is permitted.
        const RequestView* oldest = nullptr;
        for (const auto* r : list) {
            if (s.now_us - r->runtime.last_service_us >= aging_ &&
                (!oldest || std::tie(r->runtime.last_service_us, r->key) < std::tie(oldest->runtime.last_service_us, oldest->key))) oldest = r;
        }
        if (oldest) return {{next_item(*oldest, s.resources.chunk_sizes.front(), s.resources.max_batch_tokens)}, "aging_progress"};
        auto plans = builder_.candidates(s);
        if (plans.empty()) return {};
        double best = -std::numeric_limits<double>::infinity();
        std::size_t selected = 0;
        bool feasible_found = false;
        double remaining = 0;
        for (const auto& r : s.arrived) if (!terminal(r.runtime.state)) {
            remaining += static_cast<double>(r.spec.prompt_tokens - r.runtime.prefilled + r.spec.max_output_tokens - r.runtime.generated);
        }
        for (std::size_t i = 0; i < plans.size(); ++i) {
            const auto features = batch_features(plans[i], s);
            const auto cost = predictor.predict(features, s.history);
            valid_cost(cost);
            double progress = 0;
            for (const auto& item : plans[i].items) {
                const auto& r = lookup(s, item.request);
                progress += static_cast<double>(item.tokens) / static_cast<double>(r.spec.prompt_tokens + r.spec.max_output_tokens);
            }
            const double time = static_cast<double>(cost.duration_us);
            const auto tokens = features.prefill_tokens + features.decode_tokens;
            // Visible-work extrapolation, NOT a guarantee: no knowledge of future arrivals/EOS.
            const double finish_estimate = static_cast<double>(s.now_us) + time * remaining / static_cast<double>(tokens);
            const bool feasible = finish_estimate <= static_cast<double>(s.deadline_us);
            double score = progress / time;
            if (name_ == "energy") {
                if (feasible_found && !feasible) continue;
                if (feasible && !feasible_found) { best = -std::numeric_limits<double>::infinity(); feasible_found = true; }
                if (feasible) {
                    // background_w is supplied through the snapshot so candidates pay total predicted energy.
                    const double energy = cost.dynamic_j + s.background_w * time / 1e6;
                    score = energy > 0 ? progress / energy : std::numeric_limits<double>::max();
                }
            }
            if (score > best) { best = score; selected = i; }
        }
        plans[selected].reason = name_ == "speed" ? "max_predicted_normalized_progress_per_us" :
            (feasible_found ? "min_energy_per_normalized_progress_visible_deadline_estimate" : "deadline_estimate_failed_speed_fallback");
        return plans[selected];
    }
};
} // namespace
BatchPlan BatchBuilder::build(const Snapshot& s, Tokens chunk, bool decode_first, bool reverse_order) const {
    auto list = ordered(s);
    if (reverse_order) std::reverse(list.begin(), list.end());
    if (decode_first) std::stable_partition(list.begin(), list.end(), [](const auto* r) { return r->runtime.state == State::Decoding; });
    BatchPlan plan;
    Tokens budget = s.resources.max_batch_tokens;
    for (const auto* r : list) {
        if (budget == 0) break;
        auto item = next_item(*r, chunk, budget);
        // Never invent a chunk outside the allowed set. Tail shorter than any chunk is legal.
        if (item.phase == Phase::Prefill && item.tokens < std::min(chunk, r->spec.prompt_tokens - r->runtime.prefilled)) continue;
        plan.items.push_back(item);
        budget -= item.tokens;
    }
    return plan;
}
std::vector<BatchPlan> BatchBuilder::candidates(const Snapshot& s) const {
    std::vector<BatchPlan> result;
    for (const auto chunk : s.resources.chunk_sizes)
        for (const bool decode_first : {false, true})
            for (const bool reverse : {false, true}) {
                auto plan = build(s, chunk, decode_first, reverse);
                if (!plan.items.empty()) result.push_back(std::move(plan));
            }
    return result;
}
void validate_plan(const BatchPlan& plan, const Snapshot& s) {
    if (plan.items.empty()) throw std::logic_error("empty execution batch");
    std::set<RequestId> seen;
    Tokens budget = s.resources.max_batch_tokens;
    for (const auto& item : plan.items) {
        const auto& r = lookup(s, item.request);
        if (!runnable(r.runtime.state) || !seen.insert(item.request).second || item.tokens <= 0 || item.tokens > budget)
            throw std::logic_error("illegal batch item or budget");
        if (item.phase == Phase::Prefill) {
            const auto remaining = r.spec.prompt_tokens - r.runtime.prefilled;
            bool allowed = false;
            for (auto chunk : s.resources.chunk_sizes) if (item.tokens == std::min(chunk, remaining)) allowed = true;
            if (remaining <= 0 || !allowed) throw std::logic_error("illegal prefill chunk");
        } else if (item.phase != Phase::Decode || r.runtime.prefilled != r.spec.prompt_tokens || r.runtime.state != State::Decoding || item.tokens != 1) {
            throw std::logic_error("illegal decode");
        }
        budget -= item.tokens;
    }
}
BatchFeatures batch_features(const BatchPlan& plan, const Snapshot& s) {
    BatchFeatures f;
    f.sequences = plan.items.size();
    for (const auto& item : plan.items) {
        const auto& r = lookup(s, item.request);
        if (item.phase == Phase::Prefill) f.prefill_tokens += item.tokens;
        else f.decode_tokens += item.tokens;
        f.context_tokens += r.runtime.prefilled + r.runtime.generated;
    }
    return f;
}
std::unique_ptr<ISchedulingPolicy> make_policy(const std::string& name, Time aging_us) {
    if (name != "fcfs" && name != "speed" && name != "energy") throw std::invalid_argument("unknown policy: " + name);
    if (aging_us <= 0) throw std::invalid_argument("aging threshold must be positive");
    return std::make_unique<GreedyPolicy>(name, aging_us);
}
} // namespace ee538
