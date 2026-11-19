"""A/B executable regression tests. Not C's full independent trace/accounting checker."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    executable, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    fixture = json.loads((root / "examples/hand_calculated.json").read_text(encoding="utf-8"))
    expected = json.loads((root / "examples/hand_calculated.expected.json").read_text(encoding="utf-8"))
    runs = 0
    with tempfile.TemporaryDirectory(prefix="ee538-tests-") as temporary:
        folder = Path(temporary)

        def run(document, policy="fcfs", success=True):
            nonlocal runs
            runs += 1
            source, output = folder / "input.json", folder / "output.json"
            source.write_text(json.dumps(document), encoding="utf-8")
            output.unlink(missing_ok=True)
            process = subprocess.run([str(executable), str(source), policy, str(output)],
                                     capture_output=True, text=True, timeout=30)
            require((process.returncode == 0) == success,
                    f"unexpected exit {process.returncode}: {process.stderr} {process.stdout}")
            result = json.loads(output.read_text(encoding="utf-8"))
            require(result["status"] == ("success" if success else "failure"), "wrong status")
            return result

        def summary_matches(result):
            for key, value in expected["summary"].items():
                observed = result["summary"][key]
                if isinstance(value, float):
                    require(math.isclose(observed, value, rel_tol=1e-12, abs_tol=1e-12), key)
                else:
                    require(observed == value, key)
            require(result["requests"][0] == expected["request"], "request summary mismatch")
            batches = [e for e in result["events"] if e["type"] == "batch"]
            require(len(batches) == len(expected["batches"]), "batch count mismatch")
            for actual, wanted in zip(batches, expected["batches"]):
                require(all(actual[k] == v for k, v in wanted.items()), "batch timing/progress mismatch")

        for policy in ("fcfs", "speed", "energy"):
            result = run(fixture, policy)
            summary_matches(result)
            repeated = run(fixture, policy)
            result.pop("host")
            repeated.pop("host")
            require(result == repeated, "non-deterministic simulation")

            empty = copy.deepcopy(fixture)
            empty["requests"] = []
            require(run(empty, policy)["summary"]["makespan_us"] == 0, "empty workload failed")

            zero = copy.deepcopy(fixture)
            zero["requests"][0]["actual_output_tokens"] = 0
            zero_result = run(zero, policy)
            require(zero_result["summary"]["makespan_us"] == 2000, "zero-output completion failed")
            require(zero_result["requests"][0]["first_token_us"] is None, "invented first output token")

            # Boundary completion wins over cancellation at the same boundary.
            tied = copy.deepcopy(fixture)
            tied["requests"][0]["cancel_at_us"] = 3000
            require(run(tied, policy)["summary"]["completed"] == 1, "completion/cancel order failed")

            midbatch = copy.deepcopy(fixture)
            midbatch["requests"][0]["cancel_at_us"] = 500
            child = copy.deepcopy(midbatch["requests"][0])
            child.update(id="child", arrival_us=5000, dependencies=["r1"])
            child.pop("cancel_at_us")
            midbatch["requests"].append(child)
            cancelled = run(midbatch, policy)
            require(cancelled["summary"]["cancelled"] == 2, "cancellation did not propagate")
            require(cancelled["summary"]["final_kv_tokens"] == 0, "cancellation leaked KV")
            require(cancelled["requests"][0]["cancelled_us"] == 1000, "mid-batch cancellation applied too early")
            require(cancelled["requests"][1]["cancelled_us"] == 5000, "unarrived descendant visible too early")

            tied_arrival = copy.deepcopy(fixture)
            tied_arrival["requests"][0]["cancel_at_us"] = 0
            result = run(tied_arrival, policy)
            require(result["summary"]["batches"] == 0 and result["summary"]["cancelled"] == 1,
                    "cancel/arrival tie admitted a cancelled request")

            pressure = copy.deepcopy(fixture)
            pressure["requests"] += [dict(pressure["requests"][0], id="r2")]
            result = run(pressure, policy)
            require(result["summary"]["completed"] == 2 and result["summary"]["peak_kv_tokens"] == 6,
                    "exact-capacity admission failed")
            require(result["summary"]["makespan_us"] == 6000, "resource wait failed")

            short_deadline = copy.deepcopy(fixture)
            short_deadline["deadline_us"] = 1
            result = run(short_deadline, policy)
            require(not result["summary"]["deadline_met"] and result["summary"]["completed"] == 1,
                    "deadline miss silently dropped work")

        invalid_inputs = []
        for field, value in (("prompt_tokens", -1), ("actual_output_tokens", 3),
                             ("arrival_us", 0.5), ("max_output_tokens", 0), ("cancel_at_us", -1)):
            invalid = copy.deepcopy(fixture)
            invalid["requests"][0][field] = value
            invalid_inputs.append(invalid)
        invalid = copy.deepcopy(fixture)
        invalid["requests"][0]["dependencies"] = ["r1"]
        invalid_inputs.append(invalid)
        invalid = copy.deepcopy(fixture)
        invalid["requests"][0]["dependencies"] = ["unknown"]
        invalid_inputs.append(invalid)
        invalid = copy.deepcopy(fixture)
        invalid["resources"]["kv_capacity_tokens"] = 5
        invalid_inputs.append(invalid)
        invalid = copy.deepcopy(fixture)
        invalid["resources"]["chunk_sizes"] = [3]
        invalid_inputs.append(invalid)
        invalid = copy.deepcopy(fixture)
        invalid.pop("synthetic_integration_stub")
        invalid_inputs.append(invalid)
        for invalid in invalid_inputs:
            run(invalid, success=False)
        run(fixture, policy="unknown", success=False)
    print(f"PASS: {runs} executable regression runs (not performance experiments)")


if __name__ == "__main__":
    main()
