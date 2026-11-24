import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from check_result import check


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    executable = Path(sys.argv[1]).resolve()
    input_path = ROOT / "examples" / "profile_dependency.json"

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
                result = json.load(file)

            check(workload, result)

            requests = {request["id"]: request for request in result["requests"]}

            require(
                requests["r1"]["completed_us"] == 3000,
                "r1 completion time is incorrect",
            )
            require(
                requests["r2"]["admitted_us"] == 3000,
                "r2 must be admitted after r1 completes",
            )
            require(
                requests["r2"]["completed_us"] == 6000,
                "r2 completion time is incorrect",
            )

            print(f"PASS: {policy} dependency case")


if __name__ == "__main__":
    main()
