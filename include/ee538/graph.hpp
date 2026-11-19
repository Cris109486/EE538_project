#pragma once
#include "ee538/types.hpp"
namespace ee538 {
class TaskGraph {
    std::vector<std::vector<RequestId>> children_;
    std::vector<std::size_t> indegree_;
public:
    explicit TaskGraph(const std::vector<RequestInput>& requests);
    std::vector<RequestId> topological_order() const; // throws invalid_argument on a cycle
    const std::vector<RequestId>& children(RequestId id) const { return children_.at(id); }
};
} // namespace ee538
