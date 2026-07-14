# Benchmark record

This is a local engineering run, not a general performance claim.

- Date: 2026-07-14
- OS: Microsoft Windows 11 Pro 10.0.26200
- CPU: 13th Gen Intel Core i5-1345U, 12 logical processors
- Compiler: MSVC 19.44.35228.0
- Build: CMake Release, Ninja
- Dataset: 100,000 deterministic rows from `tools/generate_dataset.py`; 1,030 measurement nulls
- Timed scope: schema inference, parsing, processing, CSV output, flush, and temporary-output removal
- Command: `build-release\datapipe_benchmark.exe out\benchmark.csv 2`
- Repetitions: two per configuration, no discarded warm-up

Raw durations are milliseconds:

| Case | Chunk | Threads | Run 1 | Run 2 |
|---|---:|---:|---:|---:|
| filter | 1,000 | 1 | 587.971 | 499.390 |
| filter + projection | 1,000 | 1 | 461.822 | 400.282 |
| group + aggregate | 1,000 | 1 | 341.478 | 317.468 |
| filter | 1,000 | 12 | 509.783 | 463.571 |
| filter + projection | 1,000 | 12 | 394.021 | 338.032 |
| group + aggregate | 1,000 | 12 | 243.863 | 215.786 |
| filter | 50,000 | 1 | 392.401 | 406.820 |
| filter + projection | 50,000 | 1 | 352.027 | 378.405 |
| group + aggregate | 50,000 | 1 | 262.216 | 244.375 |
| filter | 50,000 | 12 | 373.658 | 492.188 |
| filter + projection | 50,000 | 12 | 390.299 | 397.240 |
| group + aggregate | 50,000 | 12 | 373.887 | 290.957 |

Every run read 100,000 rows. Filters wrote 49,484 rows; grouped aggregation wrote four rows.

For this small, I/O-inclusive workload, 1,000-row chunks benefited consistently from 12 workers,
especially grouped aggregation. At 50,000 rows there are only two tasks, so 12 workers cannot be
kept busy and results were noisy. Projection runs can be faster than filter-only because they write
three columns instead of all five. These observations should not be extrapolated to different
storage, row widths, CPUs, power states, or datasets.

Limitations include two repetitions, no warm-up policy, shared-machine noise, Windows filesystem
caching, no isolated CPU affinity, and end-to-end timing rather than separate parser/compute/write
measurements. Rerun the documented harness with more iterations and a larger dataset before making
capacity decisions.
