#include "ee538/cost_model.hpp"
#include "ee538/host_metrics.hpp"
#include "ee538/input.hpp"
#include "ee538/metrics.hpp"
#include "ee538/simulator.hpp"
#include "ee538/testing/synthetic_model_stub.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

template <class Profile> Profile read_profile(const nlohmann::json &j) {
  if (!j.is_object()) {
    throw std::invalid_argument("profile must be an object");
  }

  Profile p;

#define READ_FIELD(field)                                                      \
  if (j.contains(#field))                                                      \
  p.field = j.at(#field).get<double>()

  READ_FIELD(overhead_us);
  READ_FIELD(prefill_us);
  READ_FIELD(decode_us);
  READ_FIELD(sequence_us);
  READ_FIELD(context_us);
  READ_FIELD(launch_j);
  READ_FIELD(prefill_j);
  READ_FIELD(decode_j);
  READ_FIELD(quadratic_j);
  READ_FIELD(context_j);

#undef READ_FIELD

  p.validate();
  return p;
}

} // namespace
int main(int argc, char **argv) {
  using namespace ee538;
  if (argc != 4) {
    std::cerr << "Usage: ee538_scheduler INPUT.json {fcfs|speed|energy} "
                 "OUTPUT.json\n";
    return 2;
  }
  nlohmann::json result;
  int exit_code = 0;
  try {
    // Reject an accidental overwrite of the input, including path aliases.
    const auto input_path = std::filesystem::weakly_canonical(argv[1]);
    const auto output_path = std::filesystem::weakly_canonical(argv[3]);
    if (input_path == output_path)
      throw std::invalid_argument("output path must differ from input");
    std::ifstream input(argv[1]);
    if (!input)
      throw std::invalid_argument("cannot open input file");
    nlohmann::json document;
    document = nlohmann::json::parse(input);
    auto workload = parse_workload(document);
    std::unique_ptr<ICostPredictor> predictor;
    std::unique_ptr<IExecutionModel> execution;
    std::string model_status;
    const bool has_profile = document.contains("execution_profile");
    const bool has_stub = document.contains("synthetic_integration_stub");
    if (has_profile == has_stub) {
      throw std::invalid_argument("provide exactly one of execution_profile or "
                                  "synthetic_integration_stub");
    }
    if (has_profile) {
      const auto &model = document.at("execution_profile");
      predictor = std::make_unique<ProfilePredictor>(
          read_profile<ExecutionProfile>(model.at("prediction")));
      execution = std::make_unique<ProfileExecutionModel>(
          read_profile<ExecutionProfile>(model.at("execution")));
      model_status = "SYNTHETIC_PROFILE_NOT_CALIBRATED";
    } else {
      const auto &model = document.at("synthetic_integration_stub");
      predictor = std::make_unique<testing::SyntheticPredictorStub>(
          read_profile<testing::StubProfile>(model.at("prediction")));
      execution = std::make_unique<testing::SyntheticExecutionStub>(
          read_profile<testing::StubProfile>(model.at("execution")));
      model_status = "SYNTHETIC_INTEGRATION_STUB_NOT_CALIBRATED";
    }
    auto policy = make_policy(argv[2], workload.aging_us);
    const auto start = std::chrono::steady_clock::now();
    Simulator simulator(workload, *policy, *predictor, *execution);
    result = simulator.run();
    result["model_status"] = model_status;
    const auto end = std::chrono::steady_clock::now();
    MetricsCollector metrics;
    result["metrics"] = metrics.collect(workload, result);
    result["host"]["simulation_and_logging_wall_us"] =
        std::chrono::duration<double, std::micro>(end - start).count();
    const auto peak_rss = peak_process_rss_bytes();

    result["host"]["peak_process_rss_bytes"] =
        peak_rss ? nlohmann::json(*peak_rss) : nlohmann::json(nullptr);

    result["host"]["memory_measurement"] =
        peak_rss ? "getrusage_process_high_water_rss"
                 : "unsupported_or_unavailable";

    result["host"]["memory_boundary"] =
        "process lifetime through metrics collection; "
        "excludes later output serialization";
    result["host"]["timing_boundary"] =
        "construction + simulation + in-memory JSON logging; excludes input "
        "parsing and output serialization";
  } catch (const std::exception &e) {
    result = {
        {"schema_version", 1}, {"status", "failure"}, {"error", e.what()}};
    exit_code = 1;
  }
  // Never write failure JSON over input, either.
  try {
    if (std::filesystem::weakly_canonical(argv[1]) ==
        std::filesystem::weakly_canonical(argv[3])) {
      std::cerr << result.dump(2) << '\n';
      return 1;
    }
    std::ofstream output(argv[3]);
    if (!output)
      throw std::runtime_error("cannot open output file");
    output << result.dump(2) << '\n';
    if (!output)
      throw std::runtime_error("cannot write output");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  std::cout << result.at("status").get<std::string>() << '\n';
  return exit_code;
}
