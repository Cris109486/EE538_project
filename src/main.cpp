#include "ee538/input.hpp"
#include "ee538/simulator.hpp"
#include "ee538/testing/synthetic_model_stub.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
ee538::testing::StubProfile profile(const nlohmann::json& j) {
    if (!j.is_object()) throw std::invalid_argument("stub profile must be object");
    ee538::testing::StubProfile p;
#define READ_FIELD(field) if (j.contains(#field)) p.field = j.at(#field).get<double>()
    READ_FIELD(overhead_us); READ_FIELD(prefill_us); READ_FIELD(decode_us); READ_FIELD(sequence_us); READ_FIELD(context_us);
    READ_FIELD(launch_j); READ_FIELD(prefill_j); READ_FIELD(decode_j); READ_FIELD(quadratic_j); READ_FIELD(context_j);
#undef READ_FIELD
    p.validate();
    return p;
}
}
int main(int argc, char** argv) {
    using namespace ee538;
    if (argc != 4) {
        std::cerr << "Usage: ee538_scheduler INPUT.json {fcfs|speed|energy} OUTPUT.json\n";
        return 2;
    }
    nlohmann::json result;
    int exit_code = 0;
    try {
        // Reject an accidental overwrite of the input, including path aliases.
        const auto input_path = std::filesystem::weakly_canonical(argv[1]);
        const auto output_path = std::filesystem::weakly_canonical(argv[3]);
        if (input_path == output_path) throw std::invalid_argument("output path must differ from input");
        std::ifstream input(argv[1]);
        if (!input) throw std::invalid_argument("cannot open input file");
        nlohmann::json document;
        document = nlohmann::json::parse(input);
        auto workload = parse_workload(document);
        // Explicit test double: both profiles are required; no silent final-model fallback.
        const auto& stub = document.at("synthetic_integration_stub");
        testing::SyntheticPredictorStub predictor(profile(stub.at("prediction")));
        testing::SyntheticExecutionStub execution(profile(stub.at("execution")));
        auto policy = make_policy(argv[2], workload.aging_us);
        const auto start = std::chrono::steady_clock::now();
        Simulator simulator(std::move(workload), *policy, predictor, execution);
        result = simulator.run();
        const auto end = std::chrono::steady_clock::now();
        result["host"]["simulation_and_logging_wall_us"] = std::chrono::duration<double, std::micro>(end - start).count();
        result["host"]["memory_measurement"] = "pending_C_module";
        result["host"]["timing_boundary"] = "construction + simulation + in-memory JSON logging; excludes input parsing and output serialization";
    } catch (const std::exception& e) {
        result = {{"schema_version", 1}, {"status", "failure"}, {"error", e.what()}};
        exit_code = 1;
    }
    // Never write failure JSON over input, either.
    try {
        if (std::filesystem::weakly_canonical(argv[1]) == std::filesystem::weakly_canonical(argv[3])) {
            std::cerr << result.dump(2) << '\n'; return 1;
        }
        std::ofstream output(argv[3]);
        if (!output) throw std::runtime_error("cannot open output file");
        output << result.dump(2) << '\n';
        if (!output) throw std::runtime_error("cannot write output");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << result.at("status").get<std::string>() << '\n';
    return exit_code;
}
