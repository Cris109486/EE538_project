#pragma once
// C-OWNED MODULE TEST DOUBLE. Replace via interfaces; this is not the final calibrated model or metrics module.
#include "ee538/types.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ee538::testing {
struct StubProfile {
    double overhead_us{100};
    double prefill_us{400};
    double decode_us{1000};
    double sequence_us{10};
    double context_us{1};
    double launch_j{0.01};
    double prefill_j{0.08};
    double decode_j{0.1};
    double quadratic_j{0.001};
    double context_j{0.0001};
    void validate() const {
        for (const auto value : {overhead_us, prefill_us, decode_us, sequence_us, context_us, launch_j, prefill_j, decode_j, quadratic_j, context_j})
            if (!std::isfinite(value) || value < 0 || value > 1e6) throw std::invalid_argument("invalid synthetic stub parameter");
        if (overhead_us == 0 && prefill_us == 0 && decode_us == 0) throw std::invalid_argument("stub must advance time");
    }
};
inline Cost stub_cost(const BatchFeatures& f, const StubProfile& p) {
    const double prefill = static_cast<double>(f.prefill_tokens);
    const double decode = static_cast<double>(f.decode_tokens);
    const double tokens = prefill + decode;
    const double duration = p.overhead_us + p.prefill_us * prefill + p.decode_us * decode +
        p.sequence_us * static_cast<double>(f.sequences) + p.context_us * static_cast<double>(f.context_tokens);
    if (!std::isfinite(duration) || duration <= 0 || duration > 1e12) throw std::runtime_error("stub duration out of range");
    return {static_cast<Time>(std::ceil(duration)), p.launch_j + p.prefill_j * prefill + p.decode_j * decode +
        p.quadratic_j * tokens * tokens + p.context_j * static_cast<double>(f.context_tokens)};
}
class SyntheticPredictorStub final : public ICostPredictor {
    StubProfile profile_;
public:
    explicit SyntheticPredictorStub(StubProfile p = {}) : profile_(p) { profile_.validate(); }
    Cost predict(const BatchFeatures& f, const std::vector<Observation>& history) const override {
        auto c = stub_cost(f, profile_);
        // RingBuffer has a real purpose: recent observed error adapts duration prediction.
        double ratio = 0;
        for (const auto& observation : history) ratio += static_cast<double>(observation.actual.duration_us) / static_cast<double>(observation.predicted.duration_us);
        if (!history.empty()) {
            const double factor = std::max(0.5, std::min(2.0, ratio / static_cast<double>(history.size())));
            const double adjusted = std::ceil(static_cast<double>(c.duration_us) * factor);
            if (adjusted > 1e12) throw std::runtime_error("adapted duration out of range");
            c.duration_us = static_cast<Time>(adjusted);
        }
        return c;
    }
};
class SyntheticExecutionStub final : public IExecutionModel {
    StubProfile profile_;
public:
    explicit SyntheticExecutionStub(StubProfile p = {}) : profile_(p) { profile_.validate(); }
    Cost execute(const BatchFeatures& f, std::uint64_t /*batch_index*/) const override { return stub_cost(f, profile_); }
};
} // namespace ee538::testing
