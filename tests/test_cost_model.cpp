#include "ee538/cost_model.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace ee538;

void check(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

int main() {
  try {
    ExecutionProfile profile;
    BatchFeatures features{2, 0, 1, 0};

    // Hand-calculated base cost.
    const Cost base = profile_cost(features, profile);
    check(base.duration_us == 910, "incorrect base duration");
    check(std::abs(base.dynamic_j - 0.174) < 1e-9, "incorrect base energy");

    ProfilePredictor predictor(profile);
    ProfileExecutionModel execution(profile);

    // No history: prediction equals base cost.
    const Cost predicted = predictor.predict(features, {});
    check(predicted.duration_us == 910, "incorrect prediction without history");

    const Cost actual = execution.execute(features, 0);
    check(actual.duration_us == 910, "incorrect execution duration");
    check(std::abs(actual.dynamic_j - 0.174) < 1e-9,
          "incorrect execution energy");

    // Actual costs are twice the base costs.
    std::vector<Observation> history{{features, base, Cost{1820, 0.348}}};

    const Cost adjusted = predictor.predict(features, history);
    check(adjusted.duration_us == 1820, "incorrect adapted duration");
    check(std::abs(adjusted.dynamic_j - 0.348) < 1e-9,
          "incorrect adapted energy");

    // A previous corrected prediction must not undo the correction.
    history.push_back({features, adjusted, Cost{1820, 0.348}});

    const Cost repeated = predictor.predict(features, history);
    check(repeated.duration_us == 1820, "duration correction is unstable");

    // Negative parameters must be rejected.
    ExecutionProfile invalid = profile;
    invalid.prefill_us = -1;

    bool rejected = false;
    try {
      invalid.validate();
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    check(rejected, "negative parameter was accepted");

    std::cout << "Cost model tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}