#include "ee538/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace ee538 {
namespace {

using Json = nlohmann::json;

Json mean_or_null(const std::vector<double> &values) {
  if (values.empty())
    return nullptr;

  return std::accumulate(values.begin(), values.end(), 0.0) /
         static_cast<double>(values.size());
}

// Nearest-rank P95.
Json p95_or_null(std::vector<double> values) {
  if (values.empty())
    return nullptr;

  std::sort(values.begin(), values.end());

  const auto rank = static_cast<std::size_t>(
      std::ceil(0.95 * static_cast<double>(values.size())));

  return values.at(rank - 1);
}

Json divide_or_null(double numerator, double denominator) {
  if (denominator <= 0)
    return nullptr;
  return numerator / denominator;
}

} // namespace

nlohmann::json MetricsCollector::collect(const Workload &workload,
                                         const nlohmann::json &result) const {
  if (result.at("status") != "success") {
    throw std::invalid_argument("metrics require successful simulation");
  }

  std::map<std::string, const RequestInput *> inputs;
  std::optional<Time> first_arrival;

  for (const auto &request : workload.requests) {
    inputs.emplace(request.spec.id, &request);

    if (!first_arrival || request.spec.arrival_us < *first_arrival) {
      first_arrival = request.spec.arrival_us;
    }
  }

  std::vector<double> latencies;
  std::vector<double> ttfts;
  std::vector<double> admission_waits;
  std::vector<double> token_gaps;
  std::map<std::string, Time> previous_token_times;
  std::map<std::string, bool> completed_ids;

  for (const auto &request : result.at("requests")) {
    completed_ids.emplace(request.at("id").get<std::string>(),
                          request.at("state") == "completed");
  }

  if (result.contains("events")) {
    for (const auto &event : result.at("events")) {
      if (event.at("type") != "output_token")
        continue;

      const auto id = event.at("request").get<std::string>();

      // Only completed requests contribute token-gap samples.
      if (!completed_ids.at(id))
        continue;

      const Time time = event.at("time_us").get<Time>();
      const auto previous = previous_token_times.find(id);

      if (previous != previous_token_times.end()) {
        if (time < previous->second) {
          throw std::invalid_argument(
              "output token timestamps are not monotonic");
        }

        token_gaps.push_back(static_cast<double>(time - previous->second));
      }

      previous_token_times[id] = time;
    }
  }

  std::size_t completed = 0;
  std::size_t cancelled = 0;
  Time last_terminal = first_arrival.value_or(0);

  for (const auto &request : result.at("requests")) {
    const auto id = request.at("id").get<std::string>();
    const Time arrival = inputs.at(id)->spec.arrival_us;
    const auto state = request.at("state").get<std::string>();

    if (state == "completed") {
      ++completed;

      const Time end = request.at("completed_us").get<Time>();

      last_terminal = std::max(last_terminal, end);

      latencies.push_back(static_cast<double>(end - arrival));

      admission_waits.push_back(
          static_cast<double>(request.at("admitted_us").get<Time>() - arrival));

      if (!request.at("first_token_us").is_null()) {
        ttfts.push_back(static_cast<double>(
            request.at("first_token_us").get<Time>() - arrival));
      }
    } else if (state == "cancelled") {
      ++cancelled;

      last_terminal =
          std::max(last_terminal, request.at("cancelled_us").get<Time>());
    } else {
      throw std::invalid_argument("request is not terminal");
    }
  }

  const Time duration = first_arrival ? last_terminal - *first_arrival : 0;

  const double seconds = static_cast<double>(duration) / 1e6;

  const auto &summary = result.at("summary");
  const double total_j = summary.at("total_j").get<double>();
  const double generated = summary.at("generated_tokens").get<double>();

  return {{"duration_basis", "first_arrival_to_last_terminal"},
          {"duration_us", duration},
          {"completed_requests", completed},
          {"cancelled_requests", cancelled},

          {"completed_latency_samples", latencies.size()},
          {"mean_completed_latency_us", mean_or_null(latencies)},
          {"p95_completed_latency_us", p95_or_null(latencies)},

          {"completed_ttft_samples", ttfts.size()},
          {"mean_completed_ttft_us", mean_or_null(ttfts)},
          {"p95_completed_ttft_us", p95_or_null(ttfts)},

          {"mean_completed_admission_wait_us", mean_or_null(admission_waits)},
          {"completed_token_gap_samples", token_gaps.size()},
          {"mean_completed_token_gap_us", mean_or_null(token_gaps)},
          {"p95_completed_token_gap_us", p95_or_null(token_gaps)},
          {"completed_requests_per_second",
           divide_or_null(static_cast<double>(completed), seconds)},

          {"generated_tokens_per_second", divide_or_null(generated, seconds)},

          {"dynamic_j", summary.at("dynamic_j")},
          {"background_j", summary.at("background_j")},
          {"total_j", total_j},
          {"energy_basis", "engine_zero_origin_horizon"},

          {"joules_per_completed_request",
           cancelled == 0
               ? divide_or_null(total_j, static_cast<double>(completed))
               : Json(nullptr)},

          {"joules_per_generated_token", divide_or_null(total_j, generated)}};
}

} // namespace ee538