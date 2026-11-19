#include "ee538/graph.hpp"
#include <stdexcept>
namespace ee538 {
TaskGraph::TaskGraph(const std::vector<RequestInput>& requests)
    : children_(requests.size()), indegree_(requests.size()) {
    for (RequestId i = 0; i < requests.size(); ++i) {
        for (const auto parent : requests[i].spec.dependencies) {
            if (parent >= requests.size() || parent == i) throw std::invalid_argument("invalid dependency endpoint");
            for (const auto existing : children_[parent])
                if (existing == i) throw std::invalid_argument("duplicate dependency edge");
            children_[parent].push_back(i);
            ++indegree_[i];
        }
    }
}
std::vector<RequestId> TaskGraph::topological_order() const {
    auto remaining = indegree_;
    std::vector<RequestId> queue;
    for (RequestId i = 0; i < remaining.size(); ++i) if (remaining[i] == 0) queue.push_back(i);
    for (std::size_t head = 0; head < queue.size(); ++head)
        for (auto child : children_[queue[head]]) if (--remaining[child] == 0) queue.push_back(child);
    if (queue.size() != children_.size()) throw std::invalid_argument("dependency cycle");
    return queue;
}
} // namespace ee538
