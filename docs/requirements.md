# Grading focus and evidence plan

Derived from the user-supplied EE538 assignment, kickoff and proposal. The supplied material does not publish numeric grading weights. Teacher/TA scope approval and permission for AI assistance have been confirmed by the user.

The core assessment is substantive C++ data structures and algorithms integrated into an operational module, supported by correctness checks and controlled evaluation. A parser followed by two textbook algorithms is explicitly insufficient. Novelty or outperforming the baseline is not required for Phase II.

| Focus | A/B implementation draft | Evidence / current gap |
|---|---|---|
| Useful custom data structures | `BinaryHeap`, adjacency-list `TaskGraph`, `RingBuffer` | Windows Debug core tests passed on 2026-11-18 |
| Algorithmic contribution | Heap sift, Kahn traversal, capacity-constrained greedy admission and candidate packing/scoring | Tests and contract draft; review complexity limits |
| Operational completeness | Arrivals, dependencies, prefill/decode, completion/cancellation, reservation/release, termination | 38 CLI regression runs passed on Windows Debug; broader workload checks still needed |
| C++ design | C++17, encapsulated classes, templates, RAII/value ownership, const policy/model interfaces with virtual dispatch | User-triggered Windows Debug build completed; review and clean-checkout reproduction still needed |
| Correctness | Hand-calculated 3-batch example, container invariants, event/resource regressions | 2/2 CTest groups passed on Windows Debug; C's independent checker still pending |
| Fair design comparison | FCFS, speed, energy on one engine/resource contract | No comparative experiments or improvement claims yet |
| Credible models | Separated prediction/execution interfaces, explicit synthetic test double | C's final profiles, independent energy/metrics validation and sensitivity studies pending |
| Reproducible evaluation | Planned scale 10/100/1000, >=2 additional factors, >=10 workload seeds per setting, 5 timing repeats | Experiment generator, raw results, figures and machine/compiler/version capture pending |
| Final deliverables | CMake/CTest, input fixture, local instructions, contribution boundaries | Final metrics, report, README evidence map completion, demo and fixed Git version pending |

The numeric scale/seed/repeat commitments above come from the team's proposal. Report median, IQR and min/max range to satisfy both proposal and common instructions.

Scope controls: the course scheduler example describes an unchunked baseline, while this project's approved proposal/common instructions choose fixed-chunk FCFS. Preserve the project's baseline. The separate full KV-cache-manager example does not impose paging/sharing/COW requirements on this scheduler project. No GPU, API, real agent or physical tensor implementation is added.

The current Windows setup is a convenience for user-controlled local development. Ubuntu 16.04/GCC 5.4 parity remains unresolved; do not claim it has been verified. Record AI assistance and each team member's substantive review/implementation contribution honestly in the final submission.
