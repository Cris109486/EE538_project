import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from check_result import check


def must_reject(workload, result, name):
    try:
        check(workload, result)
    except ValueError:
        print(f"PASS: rejected {name}")
        return

    raise RuntimeError(f"checker failed to reject: {name}")


def main():
    executable = Path(sys.argv[1]).resolve()
    input_path = ROOT / "examples" / "profile_hand_calculated.json"

    with input_path.open(encoding="utf-8") as file:
        workload = json.load(file)

    with tempfile.TemporaryDirectory() as directory:
        for policy in ("fcfs", "speed", "energy"):
            output_path = Path(directory) / f"{policy}.json"

            subprocess.run(
                [str(executable), str(input_path), policy, str(output_path)],
                check=True,
                capture_output=True,
                text=True,
            )

            with output_path.open(encoding="utf-8") as file:
                original = json.load(file)

            check(workload, original)
            print(f"PASS: accepted {policy} result")
            broken = copy.deepcopy(original)
            batch = next(
                event for event in broken["events"] if event["type"] == "batch"
            )
            batch["context_tokens"] += 1
            must_reject(workload, broken, f"{policy}: wrong context length")
            # Change one value at a time.
            broken = copy.deepcopy(original)
            broken["summary"]["total_j"] += 1
            must_reject(workload, broken, f"{policy}: wrong total energy")

            broken = copy.deepcopy(original)
            broken["summary"]["generated_tokens"] += 1
            must_reject(workload, broken, f"{policy}: wrong output count")

            broken = copy.deepcopy(original)
            broken["summary"]["makespan_us"] += 1
            must_reject(workload, broken, f"{policy}: wrong makespan")

            broken = copy.deepcopy(original)
            batch = next(
                event for event in broken["events"] if event["type"] == "batch"
            )
            batch["actual"]["dynamic_j"] += 1
            must_reject(workload, broken, f"{policy}: wrong batch energy")

            broken = copy.deepcopy(original)
            batch = next(
                event for event in broken["events"] if event["type"] == "batch"
            )
            batch["items"].append(copy.deepcopy(batch["items"][0]))
            must_reject(workload, broken, f"{policy}: duplicate batch request")

            broken = copy.deepcopy(original)
            reservation = next(
                event for event in broken["events"] if event["type"] == "kv_reserve"
            )
            reservation["tokens"] += 1
            must_reject(workload, broken, f"{policy}: wrong KV reservation")

            broken = copy.deepcopy(original)
            broken["events"] = [
                event for event in broken["events"] if event["type"] != "kv_release"
            ]
            must_reject(workload, broken, f"{policy}: missing KV release")
            broken = copy.deepcopy(original)
            broken["metrics"]["mean_completed_latency_us"] += 1
            must_reject(workload, broken, f"{policy}: wrong latency metric")
    print("All checker tests passed")


if __name__ == "__main__":
    main()
