# Shared contracts v0.1 — integration draft

Status: draft derived from the supplied proposal and common instructions. It has not been marked team-confirmed. A maintains this document; all members review interface changes. On 2026-11-18 the user-triggered Windows Debug build completed, then the user requested assistance starting tests: both CTest groups passed, including 38 CLI regression runs. F5 debugging and VM compatibility remain unverified.

## Boundary and ownership

The core is C++17. `Simulator` value-owns input, mutable request state, graph, event heap, KV tracker and bounded observation history. Injected policy/predictor/execution objects are const references and must outlive the simulator. One simulator instance runs once.

`include/ee538/types.hpp` is the shared type source. Stable `RequestId` values are input-array indices, not pointers. External string IDs are unique; input parsing resolves dependency IDs.

| Type | Audience and role |
|---|---|
| `RequestInput` | Engine private: public metadata plus realized output length and cancellation schedule |
| `RequestSpec`, `RequestRuntime`, `RequestView` | Policy-visible metadata/progress, only after arrival |
| `Snapshot` | Current time/resources/deadline/background power, arrived requests and past completed observations |
| `BatchPlan`, `BatchItem` | Proposed request/phase/token operations plus decision reason |
| `BatchFeatures` | Phase token counts, participating sequence count, sum of their pre-batch context lengths |
| `Cost` | Positive duration and nonnegative dynamic energy; excludes background energy |
| `Observation` | Features and predicted/actual costs from a completed batch |
| `ExecutionResult` | Declared result contract for future model/metrics integration; current engine writes progress directly to batch logs |

Policies cannot receive `Workload` or `RequestInput`. Future arrivals, realized output length and future cancellation times are absent from snapshots. Dependencies may name stable IDs whose future request metadata remains hidden.

## Units and input

Time: signed 64-bit integer microseconds (`*_us`); tokens/KV: signed 64-bit integer token slots; energy: double joules; power: double watts. No token-to-byte claim is made.
Time zero is the experiment origin. Makespan and background-energy horizon start at zero, including idle time before the first arrival, and end when all requests have terminal visible states.

Open integration issue found while connecting the existing team repository: the earlier root README defines completed-workload makespan from the first arrival to final completion. The zero-origin behavior above describes the current implementation only and is not a team-confirmed replacement of that earlier definition. Align duration and background-energy reporting before comparative evaluation; no execution change is included in the repository merge.

JSON schema version is 1; see `examples/hand_calculated.json`. Token counts, times and seed must be JSON integers. Numeric configuration bounds are enforced before simulation. Prompt and maximum output lengths are positive; actual output length may be zero and cannot exceed the maximum. Every request must fit the KV capacity in isolation; otherwise input is rejected even if it has a cancellation event.

Known unsupported case: zero-length prompts. Rejecting them is a documented input-domain choice for team review, not a claim that real LLM systems cannot accept them.
Dependencies must exist, be distinct, avoid self-edges and pass Kahn topological validation. Cancellations must not precede the request's arrival.

## State and event order

Internal `NotArrived` requests are excluded from snapshots. On observation: `Waiting` → `Admitted` → `Prefilling` → `Decoding` → `Completed`, with stages skipped when appropriate. `Cancelled` is terminal. A fully prefilled request with zero actual output completes without a first-token timestamp.

Final prefill generates the first output token when actual output is nonzero. That generated token does not consume an additional decode-budget token in the same batch. Each later decode contributes exactly one output token; a request appears at most once per batch.

At a boundary:

1. Complete the running batch atomically: charge cost, apply progress, produce output timestamps, complete requests, release reservations and decrement child dependency counts.
2. Observe external events whose scheduled time is <= the boundary. Heap order is `(scheduled_us, kind, input_index)`, with cancellation before arrival when times tie. Logs preserve both scheduled and observed times.
3. Propagate cancellation through graph descendants. Unarrived descendants retain hidden `NotArrived` state and become cancelled when their arrivals are observed. A failed prerequisite never counts as successful completion.
4. Admit dependency-ready requests, construct candidates, choose and execute the next batch.

Thus completion at a boundary wins over an external cancellation observed at that boundary, including a cancellation scheduled during the just-finished batch. No partial batch rollback or preemption is implemented. A cancellation tied with its own initial arrival prevents admission. Same-batch dependencies cannot execute together because all dependencies must be completed before admission.

If no work is runnable, jump to the next external event and charge idle background energy. Never sleep for simulated time. Runnable work cannot be deliberately idled. Unfinished work without a legal batch or future event yields a defined failure.

## KV and batch legality

Reserve `prompt_tokens + max_output_tokens` at admission, keep that reservation unchanged through prefill/decode, release exactly once on completion/cancellation. Occupancy is reserved capacity, not physical tensor memory. No paging, sharing, eviction or prefix index is implied.

A batch's prefill processing tokens plus decode processing tokens cannot exceed `max_batch_tokens`. For a chosen allowed chunk `c`, prefill amount is `min(c, remaining_prompt)`. A shorter tail is legal; arbitrary truncation just to fill the remaining batch budget is not. Decode is exactly 1 token per participating request. Only admitted requests participate.

## Baseline and alternative heuristics

- FCFS: arrival time, then input index. First configured chunk size is fixed for the run. Dependency-blocked waiting requests are skipped; among dependency-ready requests, insufficient capacity at the head blocks later admissions. Active batches pack in FCFS order, skipping a chunk that cannot fit the remaining iteration budget.
- Speed/energy admission: old waiting requests first; younger requests prefer smaller conservative reservations; ties use arrival/index. Capacity backfill is allowed. Aged active requests receive a singleton batch using the first allowed chunk.
- Candidate set: each allowed chunk × FCFS/reverse order × decode-priority on/off, at most `4 * chunk_sizes.size()` candidates (duplicates are permitted). Each order is greedily packed under the shared constraints.
- Normalized candidate progress: sum of `processed_tokens / (prompt_tokens + max_output_tokens)` over participants. It counts processing work, not hidden actual output. Speed maximizes progress per predicted microsecond.
- Energy: estimate completion using `now + candidate_duration * visible_remaining_processing_tokens / candidate_processing_tokens`. It ignores unknown future arrivals and is only a coarse extrapolation. Candidates passing this visible-work estimate maximize progress per predicted total joule, including background power. If none pass, use speed scoring and log the fallback. Exact score ties keep the first enumerated candidate.

This deadline estimate is an integration heuristic, not proof of feasibility, nor a global optimum. A deadline miss is reported after completing the work; it does not drop tasks. Deadline checks across policies use the same accounting semantics. Main efficiency comparisons must use cancellation-free identical work; cancellation timing can cause different executed work even under the same external cancellation schedule.

## C integration, logs and failures

`ICostPredictor::predict(features, history)` is available to policies. Only the engine calls `IExecutionModel::execute(features, batch_index)`; policies never see that object. The temporary synthetic stub has separately configured prediction and execution parameters. It does not receive the policy's name. Recent duration errors from the custom `RingBuffer` affect its predictions. C should replace the stub through the interfaces, not redefine request/batch types.

The engine charges background power once for each disjoint execution/idle interval, `background_w * duration_us / 1e6`, and adds dynamic energy separately. A model must not include background energy in `dynamic_j`. Profile costs may depend on phase, tokens, sequence count and participating context; no fixed policy-specific saving multiplier is allowed.

JSON logs contain a monotonic event sequence, observed time, original external event time, state transitions, KV reserve/release, batch plans, decision reasons, features, prediction/execution costs and token timestamps. Current summary fields support A/B regression checks; final latency/fairness/memory metrics and CSV output remain pending C integration. Real wall time is labeled separately and includes construction, simulation and in-memory logging, excluding parsing/serialization.

Invalid input, model costs, illegal plans, stalled policies, resource inconsistencies and I/O errors throw internally and produce CLI failure JSON/nonzero exit when possible. A deadline miss has status `success` with `deadline_met=false`; it means simulation completed, not that the optimization objective succeeded. Output failure does not imply the result file exists. Invalid input/output path aliases are rejected to protect the input.

## Complexity (current implementation)

With N requests, E edges, Q queued events, R arrived requests, C allowed chunk sizes and K<=4C candidates:

- Heap push/pop O(log Q), storage O(Q); full invariant verification O(Q).
- Kahn validation O(N+E), storage O(N+E). Duplicate-edge checking during graph construction currently adds a linear scan per inserted sibling edge, so graph construction as a whole can be worse than O(N+E).
- Ring append O(1), history copy O(H); KV reservation/release O(log N) via supporting `std::map`.
- Candidate construction O(K R log R) from sorting; current ID lookup scans the visible request list, making feature/scoring validation up to O(K R^2). This is an initial implementation limit, not the proposal's idealized O(KR) bound.
- Invariant scans and full in-memory JSON logs add runtime/memory overhead. Logs grow with executed batches/events and are not a bounded-memory final streaming logger.

These bounds must be reflected in the final report or improved before claiming the proposal's target bounds.
