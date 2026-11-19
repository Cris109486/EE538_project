#include "ee538/simulator.hpp"
#include "ee538/input.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>
namespace ee538 {
bool EventBefore::operator()(const Event& a, const Event& b) const {
    return std::tie(a.time, a.kind, a.request) < std::tie(b.time, b.kind, b.request);
}
Simulator::Simulator(Workload w, const ISchedulingPolicy& policy, const ICostPredictor& predictor, const IExecutionModel& execution)
    : workload_(std::move(w)), graph_(workload_.requests), remaining_dependencies_(workload_.requests.size()),
      dependency_cancelled_(workload_.requests.size(), false), runtime_(workload_.requests.size()),
      kv_(workload_.resources.kv_capacity_tokens), history_(workload_.history_capacity),
      policy_(policy), predictor_(predictor), execution_(execution) {
    validate_workload(workload_);
    for (RequestId id = 0; id < workload_.requests.size(); ++id) {
        const auto& r = workload_.requests[id];
        remaining_dependencies_[id] = r.spec.dependencies.size();
        events_.push({r.spec.arrival_us, Event::Kind::Arrival, id});
        if (r.cancel_at_us) events_.push({*r.cancel_at_us, Event::Kind::Cancel, id});
    }
}
Snapshot Simulator::snapshot() const {
    Snapshot s;
    s.now_us = now_;
    s.deadline_us = workload_.deadline_us;
    s.background_w = workload_.background_w;
    s.resources = workload_.resources;
    s.history = history_.values();
    for (RequestId i = 0; i < runtime_.size(); ++i)
        if (runtime_[i].state != State::NotArrived) s.arrived.push_back({i, workload_.requests[i].spec, runtime_[i]});
    return s;
}
void Simulator::log(nlohmann::json e) {
    e["time_us"] = now_;
    e["seq"] = logs_.size();
    logs_.push_back(std::move(e));
}
void Simulator::transition(RequestId id, State state, const std::string& reason) {
    auto& r = runtime_.at(id);
    const auto old = r.state;
    bool legal = false;
    switch (old) {
    case State::NotArrived: legal = state == State::Waiting; break;
    case State::Waiting: legal = state == State::Admitted || state == State::Cancelled; break;
    case State::Admitted: case State::Prefilling:
        legal = state == State::Prefilling || state == State::Decoding || state == State::Completed || state == State::Cancelled; break;
    case State::Decoding: legal = state == State::Completed || state == State::Cancelled; break;
    case State::Completed: case State::Cancelled: break;
    }
    if (!legal) throw std::logic_error("illegal request state transition");
    r.state = state;
    log({{"type", "state"}, {"request", workload_.requests[id].spec.id}, {"from", state_name(old)}, {"to", state_name(state)}, {"reason", reason}});
}
void Simulator::release(RequestId id) {
    if (kv_.owns(id)) {
        const auto released = kv_.release(id);
        log({{"type", "kv_release"}, {"request", workload_.requests[id].spec.id}, {"tokens", released}, {"used_tokens", kv_.used()}});
    }
}
void Simulator::cancel(RequestId root, const std::string& reason) {
    std::vector<RequestId> queue{root};
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const auto id = queue[head];
        if (terminal(runtime_[id].state)) continue;
        if (runtime_[id].state == State::NotArrived) {
            // Defer visibility until its arrival, but remember the failed prerequisite.
            if (dependency_cancelled_[id]) continue;
            dependency_cancelled_[id] = true;
        } else {
            transition(id, State::Cancelled, head == 0 ? reason : "dependency_cancelled");
            runtime_[id].cancelled_us = now_;
            release(id);
        }
        for (const auto child : graph_.children(id)) queue.push_back(child);
    }
}
void Simulator::process_events() {
    while (!events_.empty() && events_.top().time <= now_) {
        const auto event = events_.pop();
        const auto id = event.request;
        log({{"type", "external_event"}, {"request", workload_.requests[id].spec.id},
             {"kind", event.kind == Event::Kind::Arrival ? "arrival" : "cancel"}, {"scheduled_us", event.time}});
        if (event.kind == Event::Kind::Arrival) {
            if (runtime_[id].state != State::NotArrived) throw std::logic_error("duplicate arrival");
            transition(id, State::Waiting, "arrival_observed");
            if (dependency_cancelled_[id]) cancel(id, "cancelled_before_admission");
        } else if (runtime_[id].state == State::NotArrived) {
            cancel(id, "external_cancellation");
        } else if (!terminal(runtime_[id].state)) {
            cancel(id, "external_cancellation");
        }
    }
}
bool Simulator::dependencies_complete(RequestId id) const { return remaining_dependencies_[id] == 0; }
void Simulator::admit() {
    const auto order = policy_.admission_order(snapshot());
    for (const auto id : order) {
        if (id >= runtime_.size() || runtime_[id].state != State::Waiting) throw std::logic_error("invalid admission order");
        if (!dependencies_complete(id)) continue;
        const auto& r = workload_.requests[id].spec;
        const Tokens reservation = r.prompt_tokens + r.max_output_tokens;
        if (!kv_.reserve(id, reservation)) {
            log({{"type", "admission_wait"}, {"request", r.id}, {"reason", "insufficient_kv"}, {"required_tokens", reservation}, {"available_tokens", kv_.available()}});
            // Strict FCFS head blocking; optimized policies may backfill requests that fit.
            if (std::string(policy_.name()) == "fcfs") break;
            continue;
        }
        log({{"type", "kv_reserve"}, {"request", r.id}, {"tokens", reservation}, {"used_tokens", kv_.used()}});
        transition(id, State::Admitted, "dependencies_ready_and_kv_reserved");
        runtime_[id].admitted_us = now_;
        runtime_[id].last_service_us = now_;
    }
}
void Simulator::execute(const BatchPlan& plan) {
    const auto visible = snapshot();
    validate_plan(plan, visible);
    const auto features = batch_features(plan, visible);
    const auto predicted = predictor_.predict(features, visible.history);
    const auto actual = execution_.execute(features, batches_);
    for (const auto& cost : {predicted, actual})
        if (cost.duration_us <= 0 || cost.duration_us > 1000000000000LL || !std::isfinite(cost.dynamic_j) || cost.dynamic_j < 0)
            throw std::runtime_error("invalid model cost");
    if (now_ > std::numeric_limits<Time>::max() - actual.duration_us) throw std::runtime_error("simulation time overflow");
    const Time start = now_;
    now_ += actual.duration_us;
    const double background = workload_.background_w * static_cast<double>(actual.duration_us) / 1e6;
    dynamic_j_ += actual.dynamic_j;
    background_j_ += background;
    if (!std::isfinite(dynamic_j_ + background_j_)) throw std::runtime_error("energy overflow");
    nlohmann::json items = nlohmann::json::array();
    // Log batch BEFORE its boundary state changes, so replay can apply progress atomically.
    for (const auto& item : plan.items) {
        const auto id = item.request;
        const auto& input = workload_.requests[id];
        const auto& r = runtime_[id];
        Tokens output_delta = 0;
        if (item.phase == Phase::Decode) output_delta = 1;
        else if (r.prefilled + item.tokens == input.spec.prompt_tokens && input.actual_output_tokens > 0) output_delta = 1;
        items.push_back({{"request", input.spec.id}, {"phase", item.phase == Phase::Prefill ? "prefill" : "decode"},
                         {"tokens", item.tokens}, {"prefill_delta", item.phase == Phase::Prefill ? item.tokens : 0}, {"output_delta", output_delta}});
    }
    log({{"type", "batch"}, {"batch_index", batches_}, {"start_us", start}, {"end_us", now_}, {"items", items},
         {"reason", plan.reason}, {"prefill_tokens", features.prefill_tokens}, {"decode_tokens", features.decode_tokens},
         {"sequences", features.sequences}, {"context_tokens", features.context_tokens},
         {"predicted", {{"duration_us", predicted.duration_us}, {"dynamic_j", predicted.dynamic_j}}},
         {"actual", {{"duration_us", actual.duration_us}, {"dynamic_j", actual.dynamic_j}, {"background_j", background}}}});
    for (std::size_t i = 0; i < plan.items.size(); ++i) {
        const auto id = plan.items[i].request;
        auto& r = runtime_[id];
        r.prefilled += items[i]["prefill_delta"].get<Tokens>();
        const auto output = items[i]["output_delta"].get<Tokens>();
        r.generated += output;
        r.last_service_us = now_;
        if (output > 0) {
            if (!r.first_token_us) r.first_token_us = now_;
            log({{"type", "output_token"}, {"request", workload_.requests[id].spec.id}, {"token_index", r.generated}});
        }
        if (r.prefilled == workload_.requests[id].spec.prompt_tokens && r.generated == workload_.requests[id].actual_output_tokens) {
            transition(id, State::Completed, "output_complete");
            r.completed_us = now_;
            release(id);
            for (const auto child : graph_.children(id)) {
                if (remaining_dependencies_[child] == 0) throw std::logic_error("dependency counter underflow");
                --remaining_dependencies_[child];
            }
        } else {
            const auto next = r.prefilled == workload_.requests[id].spec.prompt_tokens ? State::Decoding : State::Prefilling;
            if (r.state != next) transition(id, next, "batch_progress");
        }
    }
    history_.push({features, predicted, actual});
    ++batches_;
}
void Simulator::check_invariants() const {
    if (!kv_.invariant() || !events_.invariant() || !history_.invariant()) throw std::logic_error("container/resource invariant failed");
    for (RequestId id = 0; id < runtime_.size(); ++id) {
        const auto& r = runtime_[id];
        const auto& input = workload_.requests[id];
        const bool active = r.state == State::Admitted || r.state == State::Prefilling || r.state == State::Decoding;
        if (kv_.owns(id) != active) throw std::logic_error("reservation/state mismatch");
        if (r.prefilled < 0 || r.prefilled > input.spec.prompt_tokens || r.generated < 0 || r.generated > input.actual_output_tokens)
            throw std::logic_error("token progress out of bounds");
        if (r.generated > 0 && r.prefilled != input.spec.prompt_tokens) throw std::logic_error("output before final prefill");
        if (active && !dependencies_complete(id)) throw std::logic_error("execution before dependency completion");
        if (r.state == State::Completed && (r.prefilled != input.spec.prompt_tokens || r.generated != input.actual_output_tokens))
            throw std::logic_error("premature completion");
    }
}
nlohmann::json Simulator::run() {
    // A Simulator instance is single-use. Inputs and logs are value-owned; model/policy refs outlive run().
    if (started_) throw std::logic_error("simulator cannot be run twice");
    started_ = true;
    while (true) {
        process_events();
        check_invariants();
        bool done = true;
        for (const auto& r : runtime_) if (!terminal(r.state)) { done = false; break; }
        if (done) break;
        admit();
        const auto plan = policy_.choose(snapshot(), predictor_);
        if (!plan.items.empty()) {
            execute(plan);
            check_invariants();
        } else {
            for (const auto& r : runtime_)
                if (r.state == State::Admitted || r.state == State::Prefilling || r.state == State::Decoding)
                    throw std::logic_error("policy stalled despite runnable work");
            if (events_.empty()) throw std::runtime_error("deadlock: unfinished requests and no future event");
            const auto next = events_.top().time;
            if (next <= now_) throw std::logic_error("clock failed to advance");
            const auto energy = workload_.background_w * static_cast<double>(next - now_) / 1e6;
            log({{"type", "idle"}, {"start_us", now_}, {"end_us", next}, {"background_j", energy}});
            background_j_ += energy;
            now_ = next;
        }
    }
    check_invariants();
    nlohmann::json requests = nlohmann::json::array();
    std::size_t completed = 0, cancelled = 0;
    Tokens generated = 0;
    auto optional_time = [](const std::optional<Time>& t) -> nlohmann::json { return t ? nlohmann::json(*t) : nlohmann::json(nullptr); };
    for (RequestId id = 0; id < runtime_.size(); ++id) {
        const auto& r = runtime_[id];
        completed += r.state == State::Completed ? 1U : 0U;
        cancelled += r.state == State::Cancelled ? 1U : 0U;
        generated += r.generated;
        requests.push_back({{"id", workload_.requests[id].spec.id}, {"state", state_name(r.state)}, {"prefilled", r.prefilled}, {"generated", r.generated},
                            {"admitted_us", optional_time(r.admitted_us)}, {"first_token_us", optional_time(r.first_token_us)},
                            {"completed_us", optional_time(r.completed_us)}, {"cancelled_us", optional_time(r.cancelled_us)}});
    }
    return {{"schema_version", 1}, {"status", "success"}, {"policy", policy_.name()}, {"seed", workload_.seed},
            {"model_status", "SYNTHETIC_INTEGRATION_STUB_NOT_CALIBRATED"}, {"events", std::move(logs_)}, {"requests", std::move(requests)},
            {"summary", {{"makespan_us", now_}, {"batches", batches_}, {"completed", completed}, {"cancelled", cancelled}, {"generated_tokens", generated},
                         {"dynamic_j", dynamic_j_}, {"background_j", background_j_}, {"total_j", dynamic_j_ + background_j_},
                         {"deadline_met", now_ <= workload_.deadline_us}, {"peak_kv_tokens", kv_.peak()}, {"final_kv_tokens", kv_.used()}}}};
}
} // namespace ee538
