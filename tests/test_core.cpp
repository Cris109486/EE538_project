#include "ee538/containers.hpp"
#include "ee538/graph.hpp"
#include "ee538/input.hpp"
#include "ee538/resources.hpp"
#include "ee538/simulator.hpp"
#include "ee538/testing/synthetic_model_stub.hpp"
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
using namespace ee538;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void must_throw(F action) {
    bool threw = false;
    try { action(); } catch (const std::exception&) { threw = true; }
    require(threw, "expected exception");
}
void test_heap() {
    BinaryHeap<int> heap;
    std::multiset<int> reference;
    std::mt19937 random(538);
    must_throw([&] { (void)heap.top(); });
    must_throw([&] { (void)heap.pop(); });
    for (int iteration = 0; iteration < 3000; ++iteration) {
        if (reference.empty() || random() % 3 != 0) {
            const int value = static_cast<int>(random() % 101) - 50;
            reference.insert(value);
            heap.push(value);
        } else {
            require(heap.pop() == *reference.begin(), "heap differs from multiset");
            reference.erase(reference.begin());
        }
        require(heap.invariant(), "heap order failed");
        require(heap.size() == reference.size(), "heap size mismatch");
        if (!reference.empty()) require(heap.top() == *reference.begin(), "heap minimum mismatch");
    }
    while (!reference.empty()) {
        require(heap.pop() == *reference.begin(), "heap drain mismatch");
        reference.erase(reference.begin());
        require(heap.invariant(), "heap drain order failed");
    }
    BinaryHeap<int, std::greater<int>> max_heap;
    for (int n : {1, 4, 2, 4}) max_heap.push(n);
    for (int n : {4, 4, 2, 1}) require(max_heap.pop() == n, "heap comparator ignored");
    BinaryHeap<Event, EventBefore> events;
    events.push({1, Event::Kind::Arrival, 0});
    events.push({1, Event::Kind::Cancel, 1});
    events.push({1, Event::Kind::Cancel, 0});
    require(events.pop().request == 0, "event key tie failed");
    require(events.pop().kind == Event::Kind::Cancel, "cancel/arrival priority failed");
    require(events.pop().kind == Event::Kind::Arrival, "arrival event lost");
}
void test_ring() {
    must_throw([] { RingBuffer<int> ring(0); });
    for (std::size_t capacity : {1U, 2U, 7U}) {
        RingBuffer<int> ring(capacity);
        std::deque<int> reference;
        require(ring.values().empty(), "new ring is not empty");
        for (int n = 0; n < 100; ++n) {
            ring.push(n);
            reference.push_back(n);
            if (reference.size() > capacity) reference.pop_front();
            require(ring.values() == std::vector<int>(reference.begin(), reference.end()), "ring wrap order failed");
            require(ring.invariant(), "ring invariant failed");
        }
    }
}
void test_kv() {
    KVResourceTracker kv(10);
    require(kv.reserve(0, 6), "first reservation failed");
    require(!kv.reserve(1, 5), "capacity overflow accepted");
    require(kv.used() == 6 && kv.invariant(), "failed reservation mutated state");
    require(kv.reserve(1, 4), "exact capacity rejected");
    must_throw([&] { (void)kv.reserve(0, 1); });
    require(kv.release(0) == 6, "wrong release size");
    must_throw([&] { (void)kv.release(0); });
    require(kv.reserve(2, 6), "released capacity unavailable");
    (void)kv.release(1);
    (void)kv.release(2);
    require(kv.used() == 0 && kv.peak() == 10 && kv.invariant(), "KV lifecycle failed");
}
void test_graph() {
    std::vector<RequestInput> requests(4);
    requests[1].spec.dependencies = {0};
    requests[2].spec.dependencies = {0};
    requests[3].spec.dependencies = {1, 2};
    TaskGraph graph(requests);
    const auto order = graph.topological_order();
    std::vector<std::size_t> position(4);
    for (std::size_t i = 0; i < order.size(); ++i) position[order[i]] = i;
    for (RequestId child = 0; child < requests.size(); ++child)
        for (auto parent : requests[child].spec.dependencies)
            require(position[parent] < position[child], "topological order violates edge");
    require(graph.children(0).size() == 2, "adjacency list missing edge");
    requests[0].spec.dependencies = {3};
    must_throw([&] { (void)TaskGraph(requests).topological_order(); });
    requests[0].spec.dependencies = {4};
    must_throw([&] { (void)TaskGraph(requests); });
    requests[0].spec.dependencies.clear();
    requests[1].spec.dependencies = {0, 0};
    must_throw([&] { (void)TaskGraph(requests); });
}
testing::StubProfile hand_profile() {
    testing::StubProfile p;
    p.overhead_us = 0; p.prefill_us = 500; p.decode_us = 1000;
    p.sequence_us = 0; p.context_us = 0; p.launch_j = 0;
    p.prefill_j = 0.1; p.decode_j = 0.1; p.quadratic_j = 0; p.context_j = 0;
    return p;
}
void test_simulator(const nlohmann::json& document) {
    auto workload = parse_workload(document);
    testing::SyntheticPredictorStub predictor(hand_profile());
    testing::SyntheticExecutionStub execution(hand_profile());
    for (const std::string name : {"fcfs", "speed", "energy"}) {
        auto policy = make_policy(name, workload.aging_us);
        Simulator simulator(workload, *policy, predictor, execution);
        const auto result = simulator.run();
        require(result["summary"]["makespan_us"] == 3000, "hand makespan differs");
        require(result["summary"]["final_kv_tokens"] == 0, "KV leaked");
        require(result["requests"][0]["first_token_us"] == 2000, "final prefill must generate first token");
        require(std::abs(result["summary"]["total_j"].get<double>() - 0.503) < 1e-12, "energy accounting differs");
        must_throw([&] { (void)simulator.run(); });
    }
    // Public snapshots expose arrived metadata and completed observations only.
    class InspectPolicy final : public ISchedulingPolicy {
        std::unique_ptr<ISchedulingPolicy> delegate_ = make_policy("speed", 100000);
        void inspect(const Snapshot& s) const {
            for (const auto& request : s.arrived) {
                require(request.spec.arrival_us <= s.now_us, "future request leaked");
                require(request.runtime.state != State::NotArrived, "hidden state leaked");
                if (request.spec.id == "future") require(s.now_us >= 5000, "future metadata leaked");
            }
            require(s.history.size() <= 1, "history capacity ignored");
        }
    public:
        const char* name() const override { return "audit_speed"; }
        std::vector<RequestId> admission_order(const Snapshot& s) const override {
            inspect(s); return delegate_->admission_order(s);
        }
        BatchPlan choose(const Snapshot& s, const ICostPredictor& p) const override {
            inspect(s); return delegate_->choose(s, p);
        }
    } inspect_policy;
    auto future = workload.requests[0];
    future.spec.id = "future";
    future.spec.arrival_us = 5000;
    future.spec.dependencies = {0};
    workload.requests.push_back(future);
    workload.history_capacity = 1;
    Simulator audited(workload, inspect_policy, predictor, execution);
    require(audited.run()["summary"]["completed"] == 2, "dependency/future arrival failed");
    auto invalid = document;
    invalid["requests"][0]["dependencies"] = {"r1"};
    must_throw([&] { (void)parse_workload(invalid); });
    invalid = document;
    invalid["resources"]["kv_capacity_tokens"] = 5;
    must_throw([&] { (void)parse_workload(invalid); });
}
void test_batches() {
    Snapshot s;
    s.resources = {100, 4, {2, 4}};
    RequestView a;
    a.key = 0;
    a.spec.prompt_tokens = 3;
    a.spec.max_output_tokens = 2;
    a.runtime.state = State::Admitted;
    RequestView b = a;
    b.key = 1;
    b.runtime.state = State::Decoding;
    b.runtime.prefilled = 3;
    b.runtime.generated = 1;
    s.arrived = {a, b};
    BatchBuilder builder;
    const auto plans = builder.candidates(s);
    require(!plans.empty() && plans.size() <= 8 * s.resources.chunk_sizes.size(), "candidate bound failed");
    bool mixed = false;
    for (const auto& plan : plans) {
        validate_plan(plan, s);
        const auto features = batch_features(plan, s);
        if (features.prefill_tokens > 0 && features.decode_tokens > 0) mixed = true;
    }
    require(mixed, "no mixed prefill/decode batch constructed");
    must_throw([&] { validate_plan({{{0, Phase::Decode, 1}}, "bad"}, s); });
    must_throw([&] { validate_plan({{{0, Phase::Prefill, 2}, {0, Phase::Prefill, 2}}, "duplicate"}, s); });
    must_throw([&] { validate_plan({{{42, Phase::Prefill, 2}}, "invisible"}, s); });
    must_throw([&] { validate_plan({{{0, Phase::Prefill, 1}}, "illegal chunk"}, s); });
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected fixture path");
        std::ifstream stream(argv[1]);
        require(static_cast<bool>(stream), "cannot read fixture");
        const auto document = nlohmann::json::parse(stream);
        test_heap(); test_ring(); test_kv(); test_graph(); test_batches(); test_simulator(document);
        std::cout << "PASS: heap, graph, ring, KV, batches, lifecycle and visibility checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
