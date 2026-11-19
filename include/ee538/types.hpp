#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ee538 {
using Time = std::int64_t; // integer microseconds
using Tokens = std::int64_t;
using RequestId = std::size_t; // stable input index, never a pointer
enum class State { NotArrived, Waiting, Admitted, Prefilling, Decoding, Completed, Cancelled };
const char* state_name(State state);
inline bool terminal(State state) { return state == State::Completed || state == State::Cancelled; }

// Public scheduler metadata. No realized output length or future cancellation time.
struct RequestSpec {
    std::string id;
    std::string agent;
    Time arrival_us{};
    Tokens prompt_tokens{};
    Tokens max_output_tokens{};
    std::vector<RequestId> dependencies;
};
struct RequestInput {
    RequestSpec spec;
    Tokens actual_output_tokens{}; // driver/engine private
    std::optional<Time> cancel_at_us;
};
struct RequestRuntime {
    State state{State::NotArrived};
    Tokens prefilled{};
    Tokens generated{};
    std::optional<Time> admitted_us;
    std::optional<Time> first_token_us;
    std::optional<Time> completed_us;
    std::optional<Time> cancelled_us;
    Time last_service_us{};
};
struct RequestView {
    RequestId key{};
    RequestSpec spec;
    RequestRuntime runtime;
};
struct Resources {
    Tokens kv_capacity_tokens{};
    Tokens max_batch_tokens{};
    std::vector<Tokens> chunk_sizes;
};
enum class Phase { Prefill, Decode };
struct BatchItem { RequestId request{}; Phase phase{}; Tokens tokens{}; };
struct BatchPlan {
    std::vector<BatchItem> items;
    std::string reason;
};
struct BatchFeatures {
    Tokens prefill_tokens{};
    Tokens decode_tokens{};
    std::size_t sequences{};
    Tokens context_tokens{}; // sum of context lengths BEFORE the batch, participating requests only
};
struct Cost { Time duration_us{}; double dynamic_j{}; };
struct Observation { BatchFeatures features; Cost predicted; Cost actual; };
struct Snapshot {
    Time now_us{};
    Time deadline_us{};
    double background_w{};
    Resources resources;
    std::vector<RequestView> arrived; // no NotArrived entries
    std::vector<Observation> history; // completed observations only
};
class ICostPredictor {
public:
    virtual ~ICostPredictor() = default;
    virtual Cost predict(const BatchFeatures&, const std::vector<Observation>&) const = 0;
};
class IExecutionModel {
public:
    virtual ~IExecutionModel() = default;
    virtual Cost execute(const BatchFeatures&, std::uint64_t batch_index) const = 0;
};
struct ExecutionItem {
    RequestId request{};
    Tokens prefill_delta{};
    Tokens output_delta{};
};
struct ExecutionResult {
    Time start_us{};
    Time end_us{};
    Cost predicted;
    Cost actual;
    std::vector<ExecutionItem> progress;
};
struct Workload {
    Resources resources;
    std::vector<RequestInput> requests;
    Time deadline_us{};
    Time aging_us{100000};
    std::uint64_t seed{};
    std::size_t history_capacity{16};
    double background_w{1.0};
};
} // namespace ee538
