import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path
from statistics import median
import json

DIMENSIONS = (
    "request_count",
    "setting",
    "kv_capacity",
    "prediction_scale",
    "policy",
)

METRICS = (
    "duration_us",
    "mean_completed_latency_us",
    "p95_completed_latency_us",
    "mean_completed_ttft_us",
    "mean_completed_token_gap_us",
    "completed_requests_per_second",
    "generated_tokens_per_second",
    "total_j",
    "joules_per_completed_request",
    "joules_per_generated_token",
    "simulation_and_logging_wall_us",
    "peak_process_rss_bytes",
)


def quantile(values, probability):
    ordered = sorted(values)
    position = (len(ordered) - 1) * probability
    lower = math.floor(position)
    upper = math.ceil(position)

    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def statistics(values):
    q1 = quantile(values, 0.25)
    q3 = quantile(values, 0.75)

    return {
        "median": median(values),
        "q1": q1,
        "q3": q3,
        "iqr": q3 - q1,
        "minimum": min(values),
        "maximum": max(values),
    }


def write_csv(path, rows, fieldnames):
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def check_completeness(input_path, rows):
    manifest_path = input_path.parent / "environment.json"

    with manifest_path.open(encoding="utf-8") as file:
        experiment = json.load(file)["experiment"]

    start_seed = experiment["start_seed"]
    seed_count = experiment["seed_count"]
    repeats = experiment["repeats"]

    expected = {
        (setting, str(seed), policy, repeat)
        for setting in experiment["settings"]
        for seed in range(start_seed, start_seed + seed_count)
        for policy in experiment["policies"]
        for repeat in range(1, repeats + 1)
    }

    actual = set()

    for row in rows:
        if int(row["request_count"]) != experiment["request_count"]:
            raise ValueError("Request count differs from environment.json")

        if int(row["kv_capacity"]) != experiment["kv_capacity"]:
            raise ValueError("KV capacity differs from environment.json")

        if not math.isclose(
            float(row["prediction_scale"]),
            experiment["prediction_scale"],
            rel_tol=1e-12,
            abs_tol=1e-12,
        ):
            raise ValueError("Prediction scale differs from environment.json")

        key = (
            row["setting"],
            row["seed"],
            row["policy"],
            int(row["repeat"]),
        )

        if key in actual:
            raise ValueError(f"Duplicate run: {key}")

        actual.add(key)

    missing = expected - actual
    unexpected = actual - expected

    if missing or unexpected:
        raise ValueError(
            f"Incomplete experiment: "
            f"{len(missing)} missing runs, "
            f"{len(unexpected)} unexpected runs. "
            f"Missing examples: {sorted(missing)[:5]}"
        )

    if len(expected) != experiment["expected_runs"]:
        raise ValueError("Inconsistent expected_runs in environment.json")


def summarize(input_path, output_dir):
    with input_path.open(newline="", encoding="utf-8") as file:
        rows = list(csv.DictReader(file))

    if not rows:
        raise ValueError("Input CSV contains no experiment rows")

    required = set(DIMENSIONS) | {"seed", "repeat"} | set(METRICS)
    missing = required - set(rows[0])

    if missing:
        raise ValueError(f"Missing CSV columns: {sorted(missing)}")
    
    check_completeness(input_path, rows)

    groups = defaultdict(list)
    unique_runs = set()

    for row in rows:
        configuration = tuple(row[name] for name in DIMENSIONS)
        key = configuration + (row["seed"],)
        repeat = int(row["repeat"])

        if repeat < 1:
            raise ValueError("Repeat numbers must be positive")

        run_key = key + (repeat,)
        if run_key in unique_runs:
            raise ValueError(f"Duplicate experiment row: {run_key}")

        unique_runs.add(run_key)
        groups[key].append(row)

    per_seed = []
    across_seed = defaultdict(list)

    for key, repeated_rows in sorted(groups.items()):
        configuration = dict(zip(DIMENSIONS, key[:-1]))
        seed = key[-1]

        repeats = sorted(int(row["repeat"]) for row in repeated_rows)

        if repeats != list(range(1, len(repeats) + 1)):
            raise ValueError(f"Missing repeat numbers for {key}")

        for metric in METRICS:
            values = [float(row[metric]) for row in repeated_rows if row[metric] != ""]

            if not values:
                continue

            if len(values) != len(repeated_rows):
                raise ValueError(f"Partially missing metric: {key}, {metric}")

            if not all(math.isfinite(value) for value in values):
                raise ValueError(f"Nonfinite metric: {key}, {metric}")

            # Only measured host timing and RSS may vary between repeats.
            if metric not in (
                "simulation_and_logging_wall_us",
                "peak_process_rss_bytes",
            ):
                if any(
                    not math.isclose(value, values[0], rel_tol=1e-9, abs_tol=1e-9)
                    for value in values
                ):
                    raise ValueError(
                        f"Simulation metric changed between repeats: "
                        f"{key}, {metric}"
                    )

            stats = statistics(values)

            per_seed.append(
                {
                    **configuration,
                    "seed": seed,
                    "metric": metric,
                    "repeat_count": len(values),
                    **stats,
                }
            )

            across_seed[key[:-1] + (metric,)].append(stats["median"])

    combined = []

    for key, values in sorted(across_seed.items()):
        combined.append(
            {
                **dict(zip(DIMENSIONS, key[:-1])),
                "metric": key[-1],
                "seed_count": len(values),
                **statistics(values),
            }
        )

    # A fresh directory protects previous summaries.
    output_dir.mkdir(parents=True, exist_ok=False)

    stat_fields = ("median", "q1", "q3", "iqr", "minimum", "maximum")

    write_csv(
        output_dir / "per_seed_summary.csv",
        per_seed,
        [*DIMENSIONS, "seed", "metric", "repeat_count", *stat_fields],
    )

    write_csv(
        output_dir / "across_seed_summary.csv",
        combined,
        [*DIMENSIONS, "metric", "seed_count", *stat_fields],
    )

    print(f"Saved summaries to {output_dir}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    summarize(args.input, args.output)


if __name__ == "__main__":
    main()
