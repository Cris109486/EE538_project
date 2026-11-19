# EE538: Speed and Energy Aware Scheduling

C++17 discrete-event scheduling module for simulated agent-originated LLM requests.
Current status: **A/B initial integration draft; Windows Debug build and 2/2 CTest groups passed on 2026-11-18 (including 38 CLI regression runs). F5 debugging and VM compatibility remain unverified.**

Windows 用户自行构建、运行和调试：[操作说明](docs/local-run.md)。

## Scope and ownership

- A: event-driven driver, request state machine, custom `BinaryHeap`, adjacency-list `TaskGraph`, input validation and event logs.
- B: conservative KV reservation, greedy admission, legal mixed batches, FCFS/speed/energy scheduling policies.
- C integration: `ICostPredictor`, `IExecutionModel`, observations and a useful bounded history buffer are exposed. `testing/synthetic_model_stub.hpp` is an explicit synthetic test double. The final models, metrics/memory module, independent checker and experiments remain C's work.

This is not a complete final submission. No real LLM, GPU, agent service or hardware-energy measurement is used. No performance or energy improvement has been established.
Interface draft: [docs/contracts.md](docs/contracts.md). Grading evidence plan: [docs/requirements.md](docs/requirements.md).

## Portable command-line build

Requires C++17 and CMake >= 3.20. Ubuntu 16.04's default GCC 5.4/CMake 3.5 cannot build this configuration.
Select a compatible compiler explicitly when multiple versions exist.

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

The CLI is `ee538_scheduler INPUT.json {fcfs|speed|energy} OUTPUT.json` (add `.exe` on Windows).
Create the output directory before calling the CLI directly; `scripts/dev.ps1` creates `out/` for you.
Return codes: 0 = successful simulation (including a reported deadline miss); 1 = validation/model/execution/I/O failure; 2 = incorrect arguments.

## Reused infrastructure

`third_party/nlohmann/json.hpp`: nlohmann/json **v3.11.3**, MIT license beside the header; [upstream](https://github.com/nlohmann/json/tree/v3.11.3).
JSON parsing, CMake/CTest, Python's standard library and STL support storage are reused.
The custom heap's sift operations, graph traversal, resource lifecycle, batch construction and scheduling logic are project implementation.
AI-assisted code and configuration are present; the team must review, test, understand and record contributions before submission.
