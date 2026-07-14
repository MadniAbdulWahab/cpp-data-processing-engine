#!/usr/bin/env python3
"""Generate a deterministic CSV dataset for datapipe benchmarks."""

import argparse
import csv
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--rows", type=int, default=1_000_000)
    args = parser.parse_args()
    if args.rows < 0:
        parser.error("--rows must not be negative")

    categories = ("alpha", "beta", "gamma", "delta")
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(("id", "category", "measurement", "active", "timestamp"))
        for row_id in range(1, args.rows + 1):
            measurement = "" if row_id % 97 == 0 else f"{(row_id * 37) % 10000 / 100:.2f}"
            writer.writerow(
                (
                    row_id,
                    categories[(row_id - 1) % len(categories)],
                    measurement,
                    "true" if row_id % 2 else "false",
                    f"2025-01-{(row_id % 28) + 1:02d}T{row_id % 24:02d}:00:00Z",
                )
            )


if __name__ == "__main__":
    main()

