# vmeta performance comparison — 2026-09-21

## Revisions and evidence

- Before: `3975b9de3ddb3106a24c420ee94410dd7161d2ea`.
- After: `81da44fbde24db8e50b2704038f43232147cc62f` (merged reflection fixes).
- Valid benchmark workflow: https://github.com/lisu188/vstd/actions/runs/35645104587
- Raw CSV, JSON, harness, environment, and runner script: https://github.com/lisu188/vstd/actions/runs/35645104587/artifacts/10660121712
- Artifact SHA-256: `17fa8db0a2d913b1e92ed815ee2111a380babbb2e3423bf0adfe3522015d74b5`.

Run `35644722769` is invalid: quoted includes selected the root checkout for both binaries. It is excluded. The valid run uses angle-bracket includes and checks compiler dependency output to prove that `vmeta.h`, `vany.h`, and `vhash.h` resolve to their respective pinned checkouts.

## Method

Both revisions ran on the same Ubuntu 24.04.5 GitHub-hosted VM, AMD EPYC 7763, pinned to CPU 0. GCC 13.3.0 and Clang 18.1.3 both used libstdc++. Build flags: `-std=c++23 -O2 -DNDEBUG -pthread`; no LTO or sanitizers.

Each result is the median of nine timing samples, each containing 1,000,000 calls after 20,000 warmup calls. Before/after execution order alternated. Allocation counts and requested bytes were measured separately over 10,000 calls. Expected outputs and before/after checksums were verified. Target functions were not inlined and output barriers prevented dead-code elimination.

## GCC 13.3 results

| Operation | Before ns/call | After ns/call | Speedup | Allocations/call before → after | Requested bytes/call before → after |
|---|---:|---:|---:|---:|---:|
| Direct integer call (control) | 1.585638 | 1.594745 | 0.99x | 0 → 0 | 0 → 0 |
| Reflected integer call | 256.524446 | 122.437328 | 2.10x | 5 → 3 | 88 → 56 |
| Cached descriptor, reused arguments | 103.287477 | 19.739150 | 5.23x | 1 → 0 | 16 → 0 |
| Three integer arguments | 389.006353 | 175.186661 | 2.22x | 5 → 3 | 136 → 104 |
| Inherited method, derived-typed receiver | 356.033042 | 248.002192 | 1.44x | 7 → 5 | 120 → 104 |
| Static integer property read | 123.359279 | 45.202425 | 2.73x | 2 → 1 | 32 → 16 |
| Dynamic integer property read | 62.422264 | 37.294168 | 1.67x | 1 → 1 | 16 → 16 |
| Dynamic integer method | 238.388498 | 110.835627 | 2.15x | 5 → 3 | 88 → 56 |
| Method taking a 128-byte string by value | 434.855285 | 225.796211 | 1.93x | 15 → 7 | 1087 → 475 |

Reflected integer throughput rose from 3.90 to 8.17 million calls/second. Cached-descriptor throughput rose from 9.68 to 50.66 million calls/second. Across the nine samples, reflected integer latency ranged from 253.82–261.59 ns before and 120.24–128.41 ns after; cached-descriptor latency ranged from 102.97–104.95 ns before and 19.08–21.22 ns after.

## Clang 18.1 results

| Operation | Before ns/call | After ns/call | Speedup | Allocations/call before → after |
|---|---:|---:|---:|---:|
| Direct integer call (control) | 1.597049 | 1.615023 | 0.99x | 0 → 0 |
| Reflected integer call | 249.089184 | 124.002558 | 2.01x | 5 → 3 |
| Cached descriptor, reused arguments | 94.598130 | 23.177044 | 4.08x | 1 → 0 |
| Three integer arguments | 360.409390 | 167.993157 | 2.15x | 5 → 3 |
| Inherited method, derived-typed receiver | 333.443945 | 233.738181 | 1.43x | 7 → 5 |
| Static integer property read | 112.098287 | 41.679872 | 2.69x | 2 → 1 |
| Dynamic integer property read | 58.073010 | 34.339313 | 1.69x | 1 → 1 |
| Dynamic integer method | 230.659119 | 109.645681 | 2.10x | 5 → 3 |
| Method taking a 128-byte string by value | 428.039585 | 210.383320 | 2.03x | 15 → 7 |

Requested bytes per call matched GCC for every case. Reflected integer throughput rose from 4.01 to 8.06 million calls/second; cached-descriptor throughput rose from 10.57 to 43.15 million calls/second.

## Interpretation and limits

The tested reflection paths became faster while adding the reviewed safety fixes. Cached descriptor invocation with prebuilt arguments is allocation-free for the integer case; ordinary name-based invocation still allocates three times per call. String-value calls allocate eight fewer times and request 612 fewer bytes per call.

The direct-call control is approximately unchanged. This is a warm, single-threaded microbenchmark on one VM and one standard library, not a game FPS, startup, save/load, contention, memory-residency, or end-to-end engine benchmark. Requested allocation bytes are cumulative churn, not live heap size. No application-level improvement should be inferred without measuring that application. Small timing differences, including the direct-call control, should not be treated as significant regressions or wins.
