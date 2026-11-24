#include "ee538/metrics.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace ee538;
using Json = nlohmann::json;

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void near(const Json &value, double expected, const char *message) {
  check(value.is_number(), message);
  check(std::abs(value.get<double>() - expected) < 1e-8, message);
}

RequestInput input(const char *id, Time arrival) {
  RequestInput request;
  request.spec.id = id;
  request.spec.arrival_us = arrival;
  return request;
}

Json completed(const char *id, Time admitted, Json first_token, Time end) {
  return {{"id", id},
          {"state", "completed"},
          {"admitted_us", admitted},
          {"first_token_us", first_token},
          {"completed_us", end},
          {"cancelled_us", nullptr}};
}

Json result(Json requests, double energy, Tokens tokens) {
  return {{"status", "success"},
          {"requests", requests},
          {"summary",
           {{"total_j", energy},
            {"dynamic_j", energy},
            {"background_j", 0.0},
            {"generated_tokens", tokens}}}};
}

int main() {
  try {
    MetricsCollector collector;

    // Two requests, with the first arrival after time zero.
    Workload workload;
    workload.requests = {input("r1", 1000), input("r2", 2000)};

    auto output = result(Json::array({completed("r1", 1500, 2000, 4000),
                                      completed("r2", 2500, 3000, 6000)}),
                         2.0, 4);

    auto metrics = collector.collect(workload, output);

    check(metrics.at("duration_us") == 5000,
          "duration must exclude initial idle time");

    near(metrics.at("mean_completed_latency_us"), 3500,
         "incorrect mean latency");
    near(metrics.at("p95_completed_latency_us"), 4000, "incorrect P95 latency");
    near(metrics.at("mean_completed_ttft_us"), 1000, "incorrect TTFT");
    near(metrics.at("mean_completed_admission_wait_us"), 500,
         "incorrect admission wait");
    near(metrics.at("completed_requests_per_second"), 400,
         "incorrect request throughput");
    near(metrics.at("generated_tokens_per_second"), 800,
         "incorrect token throughput");
    near(metrics.at("joules_per_completed_request"), 1.0,
         "incorrect energy per request");
    // Interleaved requests must keep independent token timestamps.
    output["events"] = Json::array(
        {{{"type", "output_token"}, {"request", "r1"}, {"time_us", 2000}},
         {{"type", "output_token"}, {"request", "r2"}, {"time_us", 3000}},
         {{"type", "output_token"}, {"request", "r1"}, {"time_us", 4000}},
         {{"type", "output_token"}, {"request", "r2"}, {"time_us", 6000}}});

    metrics = collector.collect(workload, output);

    check(metrics.at("completed_token_gap_samples") == 2,
          "incorrect token gap sample count");
    near(metrics.at("mean_completed_token_gap_us"), 2500,
         "incorrect mean token gap");
    near(metrics.at("p95_completed_token_gap_us"), 3000,
         "incorrect P95 token gap");
    // Interleaved requests must keep independent token timestamps.
    output["events"] = Json::array(
        {{{"type", "output_token"}, {"request", "r1"}, {"time_us", 2000}},
         {{"type", "output_token"}, {"request", "r2"}, {"time_us", 3000}},
         {{"type", "output_token"}, {"request", "r1"}, {"time_us", 4000}},
         {{"type", "output_token"}, {"request", "r2"}, {"time_us", 6000}}});

    metrics = collector.collect(workload, output);

    check(metrics.at("completed_token_gap_samples") == 2,
          "incorrect token gap sample count");
    near(metrics.at("mean_completed_token_gap_us"), 2500,
         "incorrect mean token gap");
    near(metrics.at("p95_completed_token_gap_us"), 3000,
         "incorrect P95 token gap");
    // Cancellation extends the reporting interval.
    output["requests"][1] = {
        {"id", "r2"}, {"state", "cancelled"}, {"cancelled_us", 7000}};

    metrics = collector.collect(workload, output);

    check(metrics.at("completed_requests") == 1, "incorrect completed count");
    check(metrics.at("cancelled_requests") == 1, "incorrect cancelled count");
    check(metrics.at("duration_us") == 6000, "incorrect cancellation horizon");
    near(metrics.at("mean_completed_latency_us"), 3000,
         "cancelled request entered latency statistics");
    check(metrics.at("joules_per_completed_request").is_null(),
          "cancelled workload must not report energy per completion");

    // A completed request with zero output has no TTFT.
    workload.requests = {input("zero", 0)};
    output = result(Json::array({completed("zero", 0, nullptr, 1000)}), 0.2, 0);

    metrics = collector.collect(workload, output);

    check(metrics.at("completed_ttft_samples") == 0,
          "zero-output request must have no TTFT sample");
    check(metrics.at("mean_completed_ttft_us").is_null(),
          "missing TTFT must be null");
    check(metrics.at("joules_per_generated_token").is_null(),
          "zero-token energy ratio must be null");

    // Empty workload: no division by zero.
    workload.requests.clear();
    output = result(Json::array(), 0.0, 0);

    metrics = collector.collect(workload, output);

    check(metrics.at("duration_us") == 0,
          "empty workload duration must be zero");
    check(metrics.at("mean_completed_latency_us").is_null(),
          "empty latency must be null");
    check(metrics.at("completed_requests_per_second").is_null(),
          "zero-duration throughput must be null");

    std::cout << "Metrics tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}