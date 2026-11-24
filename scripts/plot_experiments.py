import argparse
import csv
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt

POLICIES = ("fcfs", "speed", "energy")

PLOTS = {
    "duration_us": ("Simulated duration (ms)", 0.001),
    "mean_completed_latency_us": ("Mean completed-request latency (ms)", 0.001),
    "total_j": ("Total simulated energy (J)", 1.0),
    "simulation_and_logging_wall_us": ("Simulation and logging runtime (ms)", 0.001),
    "peak_process_rss_bytes": ("Peak process RSS (MiB)", 1 / (1024 * 1024)),
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    with args.input.open(newline="", encoding="utf-8") as file:
        reader = csv.DictReader(file)

        required = {
            "request_count",
            "setting",
            "kv_capacity",
            "prediction_scale",
            "policy",
            "metric",
            "median",
            "q1",
            "q3",
            "seed_count",
        }

        if not required.issubset(reader.fieldnames or []):
            raise ValueError("Input is not a supported across-seed summary")

        rows = list(reader)

    groups = defaultdict(dict)

    for row in rows:
        metric = row["metric"]
        if metric not in PLOTS:
            continue

        key = (
            row["request_count"],
            row["setting"],
            row["kv_capacity"],
            row["prediction_scale"],
            metric,
        )

        policy = row["policy"]
        if policy not in POLICIES:
            raise ValueError(f"Unknown policy: {policy}")
        if policy in groups[key]:
            raise ValueError(f"Duplicate summary row: {key}, {policy}")

        groups[key][policy] = row

    if not groups:
        raise ValueError("No supported metrics found")

    args.output.mkdir(parents=True, exist_ok=False)

    for index, (key, policies) in enumerate(sorted(groups.items()), start=1):
        if set(policies) != set(POLICIES):
            raise ValueError(f"Missing policy summaries: {key}")

        request_count, setting, kv, prediction_scale, metric = key
        ylabel, scale = PLOTS[metric]

        medians = []
        lower_errors = []
        upper_errors = []
        seed_counts = set()

        for policy in POLICIES:
            row = policies[policy]

            middle = float(row["median"]) * scale
            lower = float(row["q1"]) * scale
            upper = float(row["q3"]) * scale

            if not 0 <= lower <= middle <= upper:
                raise ValueError(f"Invalid quartiles: {key}, {policy}")

            medians.append(middle)
            lower_errors.append(middle - lower)
            upper_errors.append(upper - middle)
            seed_counts.add(int(row["seed_count"]))

        if len(seed_counts) != 1:
            raise ValueError(f"Unequal seed counts: {key}")

        fig, ax = plt.subplots(figsize=(7, 4.5))

        ax.bar(
            POLICIES,
            medians,
            yerr=[lower_errors, upper_errors],
            capsize=5,
            color=("#64748b", "#2563eb", "#059669"),
        )

        ax.set_ylabel(ylabel)
        ax.set_title(
            f"{request_count} requests | {setting}\n"
            f"KV={kv}, prediction scale={prediction_scale}"
        )
        ax.set_ylim(bottom=0)
        ax.grid(axis="y", alpha=0.25)
        ax.set_axisbelow(True)

        fig.text(
            0.5,
            0.02,
            f"Median across {next(iter(seed_counts))} seeds; "
            "error bars show Q1–Q3 of per-seed medians.",
            ha="center",
            fontsize=8,
        )

        fig.tight_layout(rect=(0, 0.06, 1, 1))

        # Numeric prefix avoids relying on arbitrary CSV values as filenames.
        filename = f"{index:03d}_{metric}"

        fig.savefig(args.output / f"{filename}.png", dpi=200)
        fig.savefig(args.output / f"{filename}.pdf")
        plt.close(fig)

    print(f"Saved figures to {args.output}")


if __name__ == "__main__":
    main()
