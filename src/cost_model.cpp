#include "ee538/cost_model.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ee538 {

void ExecutionProfile::validate() const {
  for (const double value :
       {overhead_us, prefill_us, decode_us, sequence_us, context_us, launch_j,
        prefill_j, decode_j, quadratic_j, context_j}) {
    if (!std::isfinite(value) || value < 0 || value > 1e6) {
      throw std::invalid_argument("invalid execution profile parameter");
    }
  }
}

Cost profile_cost(const BatchFeatures &features,
                  const ExecutionProfile &profile) {
  profile.validate();

  if (features.prefill_tokens < 0 || features.decode_tokens < 0 ||
      features.context_tokens < 0 || features.sequences == 0 ||
      (features.prefill_tokens == 0 && features.decode_tokens == 0)) {
    throw std::invalid_argument("invalid batch features");
  }

  const double prefill = static_cast<double>(features.prefill_tokens);
  const double decode = static_cast<double>(features.decode_tokens);
  const double sequences = static_cast<double>(features.sequences);
  const double context = static_cast<double>(features.context_tokens);

  const double tokens = prefill + decode;

  const double duration = profile.overhead_us + profile.prefill_us * prefill +
                          profile.decode_us * decode +
                          profile.sequence_us * sequences +
                          profile.context_us * context;

  const double energy = profile.launch_j + profile.prefill_j * prefill +
                        profile.decode_j * decode +
                        profile.quadratic_j * tokens * tokens +
                        profile.context_j * context;

  if (!std::isfinite(duration) || duration <= 0 || duration > 1e12) {
    throw std::runtime_error("profile duration out of range");
  }

  if (!std::isfinite(energy) || energy < 0) {
    throw std::runtime_error("profile energy out of range");
  }

  return {static_cast<Time>(std::ceil(duration)), energy};
}
ProfilePredictor::ProfilePredictor(ExecutionProfile profile)
    : profile_(profile) {
  profile_.validate();
}

Cost ProfilePredictor::predict(const BatchFeatures &features,
                               const std::vector<Observation> &history) const {
  Cost result = profile_cost(features, profile_);

  double duration_ratio_sum = 0.0;
  double energy_ratio_sum = 0.0;
  std::size_t energy_samples = 0;

  for (const auto &observation : history) {
    const Cost base = profile_cost(observation.features, profile_);

    if (observation.actual.duration_us <= 0 ||
        observation.actual.duration_us > 1000000000000LL ||
        !std::isfinite(observation.actual.dynamic_j) ||
        observation.actual.dynamic_j < 0) {
      throw std::invalid_argument("invalid history observation");
    }

    const double duration_ratio =
        static_cast<double>(observation.actual.duration_us) /
        static_cast<double>(base.duration_us);

    duration_ratio_sum += std::clamp(duration_ratio, 0.5, 2.0);

    if (base.dynamic_j > 0) {
      const double energy_ratio = observation.actual.dynamic_j / base.dynamic_j;

      energy_ratio_sum += std::clamp(energy_ratio, 0.5, 2.0);
      ++energy_samples;
    }
  }

  if (!history.empty()) {
    const double factor =
        duration_ratio_sum / static_cast<double>(history.size());

    const double adjusted =
        std::ceil(static_cast<double>(result.duration_us) * factor);

    if (!std::isfinite(adjusted) || adjusted <= 0 || adjusted > 1e12) {
      throw std::runtime_error("adjusted duration out of range");
    }

    result.duration_us = static_cast<Time>(adjusted);
  }

  if (energy_samples > 0) {
    const double factor =
        energy_ratio_sum / static_cast<double>(energy_samples);

    result.dynamic_j *= factor;

    if (!std::isfinite(result.dynamic_j)) {
      throw std::runtime_error("adjusted energy out of range");
    }
  }

  return result;
}

ProfileExecutionModel::ProfileExecutionModel(ExecutionProfile profile)
    : profile_(profile) {
  profile_.validate();
}

Cost ProfileExecutionModel::execute(const BatchFeatures &features,
                                    std::uint64_t /*batch_index*/
) const {
  return profile_cost(features, profile_);
}
} // namespace ee538