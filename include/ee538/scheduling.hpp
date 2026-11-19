#pragma once
#include "ee538/types.hpp"
#include <memory>
namespace ee538 {
class BatchBuilder {
public:
    BatchPlan build(const Snapshot&, Tokens chunk, bool decode_first, bool reverse_order = false) const;
    std::vector<BatchPlan> candidates(const Snapshot&) const;
};
BatchFeatures batch_features(const BatchPlan&, const Snapshot&);
void validate_plan(const BatchPlan&, const Snapshot&);
class ISchedulingPolicy {
public:
    virtual ~ISchedulingPolicy() = default;
    virtual const char* name() const = 0;
    virtual std::vector<RequestId> admission_order(const Snapshot&) const = 0;
    virtual BatchPlan choose(const Snapshot&, const ICostPredictor&) const = 0;
};
std::unique_ptr<ISchedulingPolicy> make_policy(const std::string& name, Time aging_us);
} // namespace ee538
