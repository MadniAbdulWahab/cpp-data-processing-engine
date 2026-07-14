# Contributing

## Development workflow

Configure a warning-enabled Debug build and run the complete suite:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On Windows, regular PowerShell users should use `cmake --preset windows-msvc`, followed by the
`windows-msvc-release` build and test presets. A raw Ninja configuration requires a Visual Studio
Developer Prompt so `cl.exe` or Clang can locate the MSVC STL and Windows SDK.

Tests use a small dependency-free harness in `tests/test_main.cpp`; add deterministic fixtures under
`tests/fixtures`. Reproduce a reported failure with a focused fixture, then keep the regression test.

Format C++ changes with `clang-format -i` using the checked-in `.clang-format`. Python follows
standard four-space formatting. Keep public declarations under `include/datapipe` and implementation
details in `src`. Builds should stay warning-clean on GCC, Clang, and MSVC.

## Adding an operator

Add the public configuration value in `pipeline.hpp`, parse and validate it in the preparation stage,
then implement batch-local behavior in `process_batch`. If it has global state, define a partial
state and deterministic merge operation. Cover null behavior, incompatible types, chunk boundaries,
and one-versus-many-thread equivalence.

## Adding a reader or writer

Preserve the batch boundary: readers produce an owned `RecordBatch` plus a stable `Schema`; writers
accept rows matching their output schema. Introduce an abstract interface only after a second
implementation proves the substitution contract. Document format-specific null and malformed-input
rules and include round-trip fixtures.

## Sanitizers

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DDATAPIPE_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

AddressSanitizer and UndefinedBehaviorSanitizer are enabled together on GCC/Clang. MSVC enables its
available AddressSanitizer support. Use a separate Clang build for ThreadSanitizer when investigating
concurrency changes.

## Benchmarks

Generate data with `tools/generate_dataset.py`, build Release, and run `datapipe_benchmark` as shown
in the README. Do not commit large generated data. Preserve raw result CSV and record OS, CPU,
compiler, build type, row count, command, and run count. Treat short single-machine timings as
engineering observations, not universal throughput claims.

## Pull-request checklist

- Public behavior and limitations are documented.
- New behavior has deterministic tests, including failure cases.
- Debug and Release builds succeed without new warnings.
- CTest passes; sanitizer runs pass for ownership or concurrency changes.
- No absolute local paths, generated build outputs, or benchmark datasets are committed.
