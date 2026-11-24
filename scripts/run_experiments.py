import argparse
import copy
import csv
import json
import subprocess
import sys
from pathlib import Path
import hashlib
import os
import platform
from datetime import datetime, timezone
import math

from check_result import check

ROOT = Path(__file__).resolve().parents[1]


def run(command):
    subprocess.run(command, check=True)


def file_sha256(path):
    digest = hashlib.sha256()

    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)

    return digest.hexdigest()


def record_environment(executable, args):
    cache_path = executable.parent / "CMakeCache.txt"
    build = {}

    if cache_path.is_file():
        for line in cache_path.read_text(encoding="utf-8").splitlines():
            if line.startswith(("//", "#")) or "=" not in line:
                continue

            key_type, value = line.split("=", 1)
            key = key_type.split(":", 1)[0]

            if key in (
                "CMAKE_BUILD_TYPE",
                "CMAKE_CXX_COMPILER",
                "CMAKE_CXX_FLAGS",
                "CMAKE_CXX_FLAGS_RELEASE",
                "CMAKE_CXX_FLAGS_DEBUG",
                "EE538_SANITIZERS",
            ):
                build[key] = value

    compiler_version = None
    compiler = build.get("CMAKE_CXX_COMPILER")

    if compiler:
        try:
            completed = subprocess.run(
                [compiler, "--version"],
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
            )
            compiler_version = {
                "returncode": completed.returncode,
                "stdout": completed.stdout,
                "stderr": completed.stderr,
            }
        except (OSError, subprocess.TimeoutExpired) as error:
            compiler_version = {"error": str(error)}

    source_hashes = {}

    for folder in ("src", "include", "scripts", "tests", "examples"):
        for path in sorted((ROOT / folder).rglob("*")):
            if not path.is_file():
                continue
            if "__pycache__" in path.parts:
                continue
            if path.suffix not in (".cpp", ".hpp", ".py", ".json"):
                continue

            source_hashes[path.relative_to(ROOT).as_posix()] = file_sha256(path)

    for name in ("CMakeLists.txt",):
        path = ROOT / name
        if path.is_file():
            source_hashes[name] = file_sha256(path)

    manifest = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "system": platform.system(),
        "os_release": platform.release(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "logical_cpu_count": os.cpu_count(),
        "python_version": platform.python_version(),
        "executable": str(executable),
        "executable_sha256": file_sha256(executable),
        "cmake_cache": build,
        "compiler_version": compiler_version,
        "experiment": {
            "request_count": args.requests,
            "seed_count": args.seeds,
            "start_seed": args.start_seed,
            "repeats": args.repeats,
            "kv_capacity": args.kv_capacity,
            "prediction_scale": args.prediction_scale,
            "settings": ["linear", "overhead"],
            "policies": ["fcfs", "speed", "energy"],
            "expected_runs": args.seeds * args.repeats * 2 * 3,
        },
        "source_sha256": source_hashes,
        "timing_metric": "simulation_and_logging_wall_us",
        "timing_boundary": (
            "C++ construction, simulation, and in-memory logging; "
            "excludes parsing, metrics collection, output serialization, "
            "Python checking, and process startup"
        ),
        "memory_metric": "peak_process_rss_bytes",
        "memory_boundary": (
            "process lifetime through metrics collection; "
            "excludes later output serialization"
        ),
    }

    path = args.output / "environment.json"

    with path.open("w", encoding="utf-8") as file:
        json.dump(manifest, file, indent=2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--requests", type=int, default=10)
    parser.add_argument("--seeds", type=int, default=3)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--kv-capacity", type=int, default=256)
    parser.add_argument("--prediction-scale", type=float, default=1.0)
    parser.add_argument("--start-seed", type=int, default=538)
    parser.add_argument(
        "--executable",
        type=Path,
        default=ROOT / "build/debug/ee538_scheduler",
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.kv_capacity < 96:
        parser.error("--kv-capacity must be at least 96")

    if (
        not math.isfinite(args.prediction_scale)
        or not 0.1 <= args.prediction_scale <= 10.0
    ):
        parser.error("--prediction-scale must be between 0.1 and 10.0")
    if args.repeats < 1:
        parser.error("--repeats must be positive")

    if not 1 <= args.requests <= 1000:
        parser.error("--requests must be between 1 and 1000")
    if args.seeds < 1 or args.start_seed < 0:
        parser.error("seed count must be positive and start seed nonnegative")

    executable = args.executable.resolve()
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")

    # Use a fresh directory to avoid overwriting previous experiments.
    args.output.mkdir(parents=True, exist_ok=False)
    inputs_dir = args.output / "inputs"
    results_dir = args.output / "results"
    inputs_dir.mkdir()
    results_dir.mkdir()
    record_environment(executable, args)

    csv_path = args.output / "results.csv"

    with csv_path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = None

        for seed in range(args.start_seed, args.start_seed + args.seeds):
            base_path = inputs_dir / f"base_seed_{seed}.json"

            run(
                [
                    sys.executable,
                    str(ROOT / "scripts/generate_workload.py"),
                    "--requests",
                    str(args.requests),
                    "--seed",
                    str(seed),
                    "--output",
                    str(base_path),
                ]
            )

            with base_path.open(encoding="utf-8") as file:
                base = json.load(file)

            for setting in ("linear", "overhead"):
                workload = copy.deepcopy(base)
                workload["resources"]["kv_capacity_tokens"] = args.kv_capacity

                if setting == "overhead":
                    for name in ("prediction", "execution"):
                        profile = workload["execution_profile"][name]
                        profile["overhead_us"] = 100
                        profile["launch_j"] = 0.01

                # Scale prediction parameters only.
                # Execution parameters remain unchanged.
                prediction = workload["execution_profile"]["prediction"]

                for field in (
                    "overhead_us",
                    "prefill_us",
                    "decode_us",
                    "sequence_us",
                    "context_us",
                    "launch_j",
                    "prefill_j",
                    "decode_j",
                    "quadratic_j",
                    "context_j",
                ):
                    prediction[field] *= args.prediction_scale

                input_path = inputs_dir / f"{setting}_seed_{seed}.json"

                with input_path.open("w", encoding="utf-8") as file:
                    json.dump(workload, file, indent=2)

                # Rotate policy order across seeds and repeats.
                policies = ("fcfs", "speed", "energy")
                reference_results = {}

                for repeat in range(args.repeats):
                    offset = (seed + repeat) % len(policies)
                    ordered_policies = policies[offset:] + policies[:offset]

                    for policy in ordered_policies:
                        result_path = results_dir / (
                            f"{setting}_seed_{seed}_{policy}"
                            f"_repeat_{repeat + 1}.json"
                        )

                        run(
                            [
                                str(executable),
                                str(input_path),
                                policy,
                                str(result_path),
                            ]
                        )

                        with result_path.open(encoding="utf-8") as file:
                            result = json.load(file)

                        check(workload, result)

                        # Simulation results should be deterministic.
                        # Host timing and memory are deliberately excluded.
                        signature = {
                            key: result[key]
                            for key in (
                                "model_status",
                                "policy",
                                "seed",
                                "events",
                                "requests",
                                "summary",
                                "metrics",
                            )
                        }

                        if policy in reference_results:
                            if signature != reference_results[policy]:
                                raise ValueError(
                                    f"Nondeterministic simulation: "
                                    f"seed={seed}, model={setting}, "
                                    f"policy={policy}, repeat={repeat + 1}"
                                )
                        else:
                            reference_results[policy] = signature

                        row = {
                            "request_count": args.requests,
                            "setting": setting,
                            "kv_capacity": args.kv_capacity,
                            "prediction_scale": args.prediction_scale,
                            "seed": seed,
                            "policy": policy,
                            "repeat": repeat + 1,
                            "model_status": result["model_status"],
                            "batches": result["summary"]["batches"],
                            "peak_kv_tokens": (result["summary"]["peak_kv_tokens"]),
                            "deadline_met": result["summary"]["deadline_met"],
                            "simulation_and_logging_wall_us": (
                                result["host"]["simulation_and_logging_wall_us"]
                            ),
                            "peak_process_rss_bytes": (
                                result["host"].get("peak_process_rss_bytes")
                            ),
                            **result["metrics"],
                        }

                        if writer is None:
                            writer = csv.DictWriter(csv_file, fieldnames=list(row))
                            writer.writeheader()

                        writer.writerow(row)
                        csv_file.flush()

                        print(
                            f"PASS: requests={args.requests}, "
                            f"seed={seed}, model={setting}, "
                            f"policy={policy}, "
                            f"repeat={repeat + 1}/{args.repeats}"
                        )
    print(f"Results saved to {csv_path}")


if __name__ == "__main__":
    main()
