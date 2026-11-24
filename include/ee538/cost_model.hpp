#pragma once

#include "ee538/types.hpp"

namespace ee538 {

// Synthetic model parameters; not hardware measurements.
// Energy parameters exclude background energy.
struct ExecutionProfile {
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

  void validate() const;
};

// Compute a batch's base duration and dynamic energy.
Cost profile_cost(const BatchFeatures &features,
                  const ExecutionProfile &profile);

class ProfilePredictor final : public ICostPredictor {
private:
  ExecutionProfile profile_;

public:
  explicit ProfilePredictor(ExecutionProfile profile = {});

  Cost predict(const BatchFeatures &features,
               const std::vector<Observation> &history) const override;
};

class ProfileExecutionModel final : public IExecutionModel {
private:
  ExecutionProfile profile_;

public:
  explicit ProfileExecutionModel(ExecutionProfile profile = {});

  Cost execute(const BatchFeatures &features,
               std::uint64_t batch_index) const override;
};

} // namespace ee538