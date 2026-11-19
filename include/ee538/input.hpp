#pragma once
#include "ee538/types.hpp"
#include <nlohmann/json.hpp>
namespace ee538 {
Workload parse_workload(const nlohmann::json& input);
void validate_workload(const Workload& workload);
} // namespace ee538
