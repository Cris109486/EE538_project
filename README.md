# Speed and Energy Aware Scheduling for LLM Agent Requests

EE538 Fall 2026 Final Project · Track B: LLM and MLSys Module

Repository: [Cris109486/EE538_project](https://github.com/Cris109486/EE538_project)

## Project status

**Phase I: proposal and interface design.** The features and experiments below describe the planned Phase II implementation. Build, test, and execution commands will be added as working components become available.

## Overview

We will implement a C++17 discrete-event inference scheduler to study completion time, throughput, and energy consumption for agent-originated LLM calls. Agent workflows are represented as requests connected by dependencies, such as independent calls, chains, branches, and fork-join workflows.

The scheduler will choose which requests to admit and how to construct batches under KV-cache capacity and per-iteration token limits. We will compare a fixed-chunk FCFS baseline with speed-priority and energy-priority policies.

Our main question is: **when do chunked prefill and cost-aware batch selection improve the speed-energy trade-off under KV pressure?**

## Team

| Member | Contact | Planned responsibilities |
| --- | --- | --- |
| Cris Hu | jinhaoh@usc.edu | Driver, state machine, TaskGraph/BinaryHeap, input validation, and integration |
| Zheyu Yang | zheyuyan@usc.edu | KV accounting, batch construction, and scheduling policies |
| Alicia Ma | xiaodanm@usc.edu | C++ execution profiles, history/energy/metrics, and independent Python checks and experiments |

Each member will test their own modules, cross-review a teammate's work, and contribute to the report and demonstration.

## Inputs and outputs

Inputs will come from team-written deterministic test fixtures and a workload generator with recorded random seeds. Initial experiments will use synthetic requests and execution profiles.

Planned JSON inputs describe:

- Requests: IDs, arrival times, prompt token counts, output limits and estimates, dependencies, and optional cancellation events.
- System configuration: KV capacity, per-iteration token budget, allowed prefill chunk sizes, and workflow deadline.
- Execution profiles: predicted and simulated execution time and energy as functions of phase, batch tokens, active sequences, and context length.

The driver will keep realized output lengths private. Scheduling policies will see only output limits and estimates. Prompt text and actual token generation are outside the simulator's scope.

Outputs will include batch/event logs, CSV summaries, and plots showing timelines, speed-energy trade-offs, and scaling behavior. Background energy will be accounted for separately.

## Scheduling policies

| Policy | Planned behavior |
| --- | --- |
| Fixed-chunk FCFS | Baseline using arrival order and a fixed prefill chunk size |
| Speed priority | Prefer candidate batches with higher predicted normalized progress per unit time |
| Energy priority | Prefer candidate batches with higher predicted normalized progress per joule, subject to an estimated workflow-deadline check |

All policies will share the same workload, resource limits, and accounting rules. Candidate construction will consider a bounded set of prefill chunks and request combinations. Deterministic tie-breaking and waiting-time aging will address repeatability and starvation.

The alternative policies are heuristics. Their deadline checks do not prove feasibility or global optimality.

## Simulation behavior

The C++ core will handle request admission, partial prefill, one-token decode steps, completion, cancellation, dependency release, and batch selection.

- Requests become eligible after arrival and successful completion of all dependencies.
- KV capacity is conservatively reserved using prompt length plus maximum output length.
- Reservations are released on completion or cancellation.
- Arrivals and cancellations during a batch take effect at its next boundary.
- The final prefill step produces the first output token; subsequent decode steps each produce one additional token.

### Hand-checkable fixture

A request has 4 prompt tokens, 2 output tokens, and a prefill chunk size of 2.

| Step | Work | Time | Energy |
| --- | --- | --- | --- |
| Prefill 1 | Process 2 prompt tokens | 1 ms | 0.2 J |
| Prefill 2 | Process the remaining 2 prompt tokens and produce the first output token | 1 ms | 0.2 J |
| Decode | Produce the second output token | 1 ms | 0.1 J |
| Total | Complete the request | 3 ms | 0.5 J |

These are test assumptions, excluding background energy, rather than hardware measurements.

## C++ design and course coverage

Planned custom data structures are `BinaryHeap<T, Compare>` for events, an adjacency-list `TaskGraph`, and `RingBuffer<T>` for recent prediction-error statistics. Core algorithms include Kahn topological validation and resource-constrained greedy admission and batch construction.

Main classes will include `Simulator`, `KVResourceTracker`, `BatchBuilder`, `CostModel`, `EnergyLedger`, and `MetricsCollector`. An `ISchedulingPolicy` interface will provide interchangeable policies through virtual dispatch. Resource management will use RAII, value ownership, and stable request IDs.

Core scheduling and execution logic will be implemented in C++. STL containers, a JSON parser, and CMake will provide supporting infrastructure. Python will support workload generation, independent checking, experiments, and plotting.

## Correctness and evaluation

Hand-calculated fixtures and an independent Python checker will verify request progress, batch token counts, dependencies, capacity reservation and release, time intervals, energy, and summary metrics. Tests will cover data-structure invariants, capacity boundaries, mixed phases, tied events, arrivals and cancellations during a batch, invalid inputs, starvation, and termination. CTest and sanitizers will be used where supported.

Experiments will use 10, 100, and 1,000 requests. We will vary prompt/output lengths, arrival burstiness, dependency shape, KV capacity, chunk size, deadlines, execution-profile shape, and prediction error. Each setting will use at least 10 workload seeds; C++ timing measurements will be repeated five times and summarized using the median and interquartile range.

| Dimension | Planned metrics |
| --- | --- |
| Completion and latency | Makespan, mean/P95 request latency, TTFT, inter-token gap, waiting time, and deadline success |
| Throughput | Completed requests per second and generated output tokens per second |
| Energy | Total energy, component energy, joules per request, and joules per output token |
| Resources and overhead | Peak KV use, measured C++ runtime, and peak process memory |

For completed workloads, makespan will span the first arrival to the final completion. Request throughput is completed requests divided by this duration; output-token throughput is generated output tokens divided by the same duration. Cancellation experiments will report completed and cancelled requests separately.

Measured C++ runtime and process memory will be reported separately from simulated LLM time and energy. Synthetic profiles will support conclusions within the stated model. Hardware-energy claims will require appropriate calibration and validation data.

## Resources and implementation plan

The planned environment is a standard CPU laptop with a C++17 compiler, CMake/CTest, Git, Python 3, a JSON library, NumPy, and Matplotlib. The planning budget is 8 GB RAM and 2 GB storage. The core simulator requires no GPU or paid service.

| Stage | Planned deliverable |
| --- | --- |
| Week 1 after approval | Input/state contracts, reference checker, and a hand-checkable request lifecycle |
| Week 2 | Integrated request lifecycles, KV accounting, and scheduling policies |
| Week 3 | Controlled experiments, scaling, and model-sensitivity analysis |
| Final days | Clean-checkout reproduction, README evidence map, report, demo, and a fixed Git tag |

The main risk is dependence on an uncalibrated cost model. We will document assumptions, test multiple profile shapes and prediction errors, and report cases where an alternative policy performs worse.

## Scope

The prototype will cover one request lifecycle, fixed costs, and fixed-chunk FCFS. The complete Phase II deliverable will include interacting request lifecycles, custom structures, dependency management, both scheduling objectives, independent checks, and reproducible experiments.

Real LLM execution, GPU kernels, physical KV buffers and paging, hardware power control, and production serving are outside the project scope. Optional extensions are measured execution-profile calibration and a Power project interface. Phase III is not part of the current plan.

## References

1. G.-I. Yu et al., [Orca: A Distributed Serving System for Transformer-Based Generative Models](https://www.usenix.org/conference/osdi22/presentation/yu), OSDI, 2022. Background for iteration-level scheduling.
2. A. Agrawal et al., [Taming Throughput-Latency Tradeoff in LLM Inference with Sarathi-Serve](https://www.usenix.org/conference/osdi24/presentation/agrawal), OSDI, 2024. Background for chunked prefill and mixed prefill/decode batching.
3. W. Kwon et al., [Efficient Memory Management for Large Language Model Serving with PagedAttention](https://arxiv.org/abs/2309.06180), SOSP, 2023. Background for KV-capacity constraints in serving variable-length requests.
4. A. Agrawal et al., [Vidur: A Large-Scale Simulation Framework for LLM Inference](https://proceedings.mlsys.org/paper_files/paper/2024/hash/b74a8de47d2b3c928360e0a011f48351-Abstract-Conference.html), MLSys, 2024. Background for profile-based simulation.
5. J. Stojkovic et al., [Towards Greener LLMs: Bringing Energy-Efficiency to the Forefront of LLM Inference](https://arxiv.org/abs/2403.20306), arXiv:2403.20306, 2024. Background for evaluating energy alongside latency and throughput.

These references motivate individual mechanisms. This course project will implement its own bounded simulator and scheduling policies; it does not reproduce the complete systems or their published hardware results.
