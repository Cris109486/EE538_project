#pragma once
#include "ee538/containers.hpp"
#include "ee538/graph.hpp"
#include "ee538/resources.hpp"
#include "ee538/scheduling.hpp"
#include <nlohmann/json.hpp>
namespace ee538 {
struct Event {
    Time time{};
    enum class Kind { Cancel, Arrival } kind{};
    RequestId request{};
};
struct EventBefore { bool operator()(const Event& a, const Event& b) const; };
class Simulator {
    Workload workload_;
    TaskGraph graph_;
    std::vector<std::size_t> remaining_dependencies_;
    std::vector<bool> dependency_cancelled_;
    std::vector<RequestRuntime> runtime_;
    KVResourceTracker kv_;
    BinaryHeap<Event, EventBefore> events_;
    RingBuffer<Observation> history_;
    const ISchedulingPolicy& policy_;
    const ICostPredictor& predictor_;
    const IExecutionModel& execution_;
    Time now_{};
    std::uint64_t batches_{};
    bool started_{};
    double dynamic_j_{};
    double background_j_{};
    nlohmann::json logs_ = nlohmann::json::array();
    Snapshot snapshot() const;
    void log(nlohmann::json event);
    void transition(RequestId, State, const std::string& reason);
    void release(RequestId);
    void process_events();
    void cancel(RequestId, const std::string& reason);
    bool dependencies_complete(RequestId) const;
    void admit();
    void execute(const BatchPlan&);
    void check_invariants() const;
public:
    Simulator(Workload, const ISchedulingPolicy&, const ICostPredictor&, const IExecutionModel&);
    nlohmann::json run();
};
} // namespace ee538
