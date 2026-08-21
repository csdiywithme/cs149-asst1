# SAXPY: SIMD, Tasks, and the Memory-Bandwidth Ceiling

This experiment studies the BLAS SAXPY operation

```text
result[i] = scale * X[i] + Y[i]
```

over 20 million single-precision elements. Although every element is
independent, the kernel performs very little arithmetic per byte transferred.
On an Apple M1, the serial, single-core ISPC, and tasked ISPC implementations
therefore converge on the same shared memory-bandwidth ceiling instead of
scaling linearly with the number of CPU cores.

## Platform and method

- Apple M1 with four performance cores and four efficiency cores
- Native arm64 C++ build with `-O2`
- ISPC 1.31 targeting `neon-i32x8`
- `N = 20,000,000`, so each input or output array occupies 80 MB
- 64 equal-sized ISPC tasks
- Three trials per implementation; the supplied harness reports the minimum
- ISPC and task results checked element-by-element against the serial result

The M1's Neon registers are 128 bits wide and hold four FP32 values. The ISPC
target has a software gang width of eight, so ISPC lowers each gang to multiple
Neon operations. This distinction does not materially change SAXPY's limiting
factor: the kernel is dominated by moving arrays through the memory hierarchy.

## Baseline result

The timings below are medians from seven interleaved executions of the final
streaming-store build. Each execution itself reports the best of three trials.

| Implementation | Time | Effective bandwidth | Throughput |
|---|---:|---:|---:|
| Serial C++ | 4.570 ms | 65.206 GiB/s | 8.752 GFLOP/s |
| ISPC, no tasks | 4.504 ms | 66.169 GiB/s | 8.881 GFLOP/s |
| ISPC, 64 tasks | 4.348 ms | 68.543 GiB/s | 9.200 GFLOP/s |

The observed task benefit is only `4.504 / 4.348 = 1.04x`. Individual runs
varied enough that the printed task speedup ranged on both sides of `1.0x`.
The important observation is therefore the plateau, not a particular hundredth
of a speedup value.

SAXPY performs one multiply and one add per element, but transfers several
floats. Under the conventional-store traffic model its arithmetic intensity is

```text
2 FLOPs / 16 bytes = 0.125 FLOP/byte.
```

This is a memory-bound workload. A single optimized sequential stream already
uses a large fraction of available bandwidth. Additional tasks share the same
cache and DRAM paths; they do not create more memory bandwidth. Task launch,
scheduling, synchronization, heterogeneous core speeds, and normal run-to-run
system noise can consequently erase the small remaining opportunity for
parallel speedup. Rewriting only the loop cannot produce near-linear multicore
scaling once bandwidth is saturated.

The C++ baseline is also compiled with optimization enabled. Its loop is simple
enough for the compiler to vectorize, so “serial C++” means one application
thread, not necessarily scalar machine instructions. This helps explain why
single-core ISPC does not substantially outperform it.

## Why `TOTAL_BYTES` uses a multiplier of four

The source estimates traffic as

```cpp
TOTAL_BYTES = 4 * N * sizeof(float);
```

For a conventional write-back, write-allocate cache, the four streams are:

1. read `X[i]`;
2. read `Y[i]`;
3. fetch ownership of the cache line containing `result[i]` on a store miss;
4. eventually write the dirty result cache line back.

Amortized over complete cache lines, this is 16 bytes of traffic per element,
not merely the 12 bytes visible in the source expression. The bandwidth printed
by the program is a model-derived effective bandwidth, not a hardware-counter
measurement of physical DRAM traffic.

## Streaming-store experiment

The output is written sequentially and is not read again inside the kernel, so
it is a reasonable candidate for a non-temporal store. The task implementation
uses ISPC `streaming_store` for complete eight-instance gangs and a normal store
for the short tail. Disassembly confirms that ISPC emits an AArch64 `stnp` pair
for the streaming path, compared with `stp` for the otherwise identical normal
path.

I compared two binaries whose task loops differed only at that store. Seven
executions were interleaved to reduce bias from temperature and background
activity; every execution still used the harness's best of three trials.

| Task-store policy | Median task time | Range | Effective bandwidth |
|---|---:|---:|---:|
| Normal store | 5.051 ms | 4.954-5.868 ms | 59.006 GiB/s |
| `streaming_store` | **4.348 ms** | 4.302-4.674 ms | 68.543 GiB/s |

The controlled median improved by about `13.9%`. My first few standalone runs
did not make this difference obvious because the benchmark varies by a similar
amount, which is why repeated, interleaved measurements matter. The optimization
is real on this run, but it still does not produce a large end-to-end task
speedup or anything close to linear multicore scaling.

If a non-temporal store completely avoided the result read-for-ownership, the
traffic model would fall from 16 to 12 bytes per element. Even in that idealized
case, a purely bandwidth-limited upper bound is only `16 / 12 = 1.33x`. In
practice, `stnp` is a cache-policy hint rather than a guarantee, the memory
controller may combine or allocate writes differently, and task/runtime costs
remain. The observed 1.14x store-policy improvement is consistent with those
limits.

When streaming stores do avoid allocation, the program's reported bandwidth
still divides 16 bytes per element by time. It should then be interpreted as
effective bandwidth relative to the original write-allocate model; actual data
traffic may be closer to 12 bytes per element.

## Conclusion

- SAXPY exposes ample element-level parallelism but very low arithmetic
  intensity.
- SIMD removes instruction throughput as the main concern; memory bandwidth
  becomes the bottleneck.
- Sixty-four tasks do not scale linearly because all cores share the same memory
  system, and task overhead can outweigh the small remaining headroom.
- Streaming stores reduce unnecessary output-cache traffic in principle. They
  produced a modest controlled improvement here, but not the substantial
  end-to-end acceleration requested by the open-ended extra-credit question.
- A larger improvement would normally require reducing whole-array traffic,
  for example by fusing SAXPY with a consumer of `result`, rather than optimizing
  this isolated pass alone.

## Build and reproduce

The Makefile selects `neon-i32x8` on Apple Silicon and `avx2-i32x8` on x86-64.
It prefers the repository-local ISPC compiler when present. Streaming stores are
enabled by default:

```bash
cd prog5_saxpy
make clean
make
./saxpy
```

For the normal-store control, clean first because Make does not track changes to
command-line flags as dependencies:

```bash
make clean
make STREAMING_STORE=0
./saxpy
```

Restore the final streaming-store build with:

```bash
make clean
make STREAMING_STORE=1
./saxpy
```

References: [ISPC User's Guide](https://ispc.github.io/ispc.html) and
[Arm A64 instruction set guide](https://developer.arm.com/documentation/ddi0602/latest/).
