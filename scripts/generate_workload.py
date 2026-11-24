import argparse
import copy
import json
import random
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--requests", type=int, default=10)
    parser.add_argument("--seed", type=int, default=538)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if not 1 <= args.requests <= 1000:
        parser.error("--requests must be between 1 and 1000")

    root = Path(__file__).resolve().parents[1]
    template_path = root / "examples" / "profile_hand_calculated.json"

    if args.output.resolve() == template_path.resolve():
        parser.error("output must differ from template")

    with template_path.open(encoding="utf-8") as file:
        workload = copy.deepcopy(json.load(file))

    rng = random.Random(args.seed)

    workload["seed"] = args.seed
    workload["deadline_us"] = 1_000_000_000
    workload["resources"] = {
        "kv_capacity_tokens": 256,
        "max_batch_tokens": 32,
        "chunk_sizes": [4, 8, 16, 32],
    }

    requests = []
    arrival = 0

    for index in range(args.requests):
        arrival += rng.randint(0, 2000)

        prompt = rng.randint(4, 64)
        output_limit = rng.randint(4, 32)
        actual_output = rng.randint(1, output_limit)

        requests.append(
            {
                "id": f"r{index}",
                "agent": f"agent{index % 3}",
                "arrival_us": arrival,
                "dependencies": [],
                "prompt_tokens": prompt,
                "max_output_tokens": output_limit,
                "actual_output_tokens": actual_output,
            }
        )

    workload["requests"] = requests

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as file:
        json.dump(workload, file, indent=2)

    print(f"Created {args.output} with {args.requests} requests")


if __name__ == "__main__":
    main()
