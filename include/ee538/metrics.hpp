#pragma once

#include "ee538/types.hpp"

#include <nlohmann/json.hpp>

namespace ee538 {

class MetricsCollector {
public:
  // Called after simulation. Workload is used only for reporting.
  nlohmann::json collect(const Workload &workload,
                         const nlohmann::json &result) const;
};

} // namespace ee538