import argparse
import csv
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    if args.input.resolve() == args.output.resolve():
        parser.error("output must differ from input")

    with args.input.open(encoding="utf-8") as file:
        result = json.load(file)

    if result.get("status") != "success":
        raise ValueError("input must contain a successful simulation")

    metrics = result["metrics"]
    row = {
        "policy": result["policy"],
        "seed": result["seed"],
        "model_status": result["model_status"],
        **metrics,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)

    with args.output.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=list(row))
        writer.writeheader()
        writer.writerow(row)

    print(f"Saved: {args.output}")


if __name__ == "__main__":
    main()
