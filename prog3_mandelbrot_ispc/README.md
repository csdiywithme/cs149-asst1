# Mandelbrot with ISPC SIMD and Tasks

This program renders the Mandelbrot set with two layers of CPU parallelism:
ISPC `foreach` maps independent pixels to SIMD program instances, while ISPC
tasks distribute horizontal image strips across CPU cores. The final version
preserves pixel-for-pixel equivalence with the serial reference and reaches a
19.32x end-to-end speedup on an Apple M1.

## Execution model

The non-tasked ISPC implementation executes a gang of program instances on one
core. The build targets `neon-i32x8`, so the gang contains eight instances.
This is an ISPC abstraction width, not the width of one hardware register. Arm
Neon registers are 128 bits wide and therefore hold four 32-bit floating-point
values; ISPC lowers an eight-instance gang to multiple Neon operations.

Mandelbrot is not ideal SIMD work because adjacent pixels can require very
different iteration counts. Once a pixel escapes, its lane becomes inactive,
but the gang continues until its slowest active lane finishes. Divergence is
especially pronounced near the fractal boundary, which explains why View 2
has a lower single-core SIMD speedup than View 1.

The tasking implementation adds multicore parallelism. Each task owns a
horizontal strip and uses `foreach` within that strip. The task count is a
build-time setting, and ceiling division plus a bounded final strip keep the
partition correct if the image height is not evenly divisible.

## Task granularity

The supplied implementation launched only two tasks, which could use at most
two cores and left performance sensitive to the cost of the two image halves.
I swept the task count on View 1 and repeated the best-performing candidates.

| Tasks | Multicore ISPC time |
|---:|---:|
| 1 | 59.314 ms |
| 2 | 31.456 ms |
| 4 | 24.598 ms |
| 8 | 16.598 ms |
| 20 | 11.93-12.22 ms |
| 32 | 11.16-11.43 ms |
| 40 | 10.90-11.09 ms |
| 80 | 10.81-11.00 ms |
| 100 | **10.73-11.00 ms** |
| 400 | 10.80-11.12 ms |
| 800 | 10.86-11.39 ms |

Performance improves rapidly until there are enough runnable tasks for all
cores. Additional decomposition then reduces the tail caused by expensive
Mandelbrot regions and by the M1's heterogeneous performance and efficiency
cores. Results plateau around 40-400 tasks; 100 tasks was the fastest stable
configuration and gives each task eight complete image rows. At one task per
row, scheduling overhead begins to offset the remaining load-balance benefit.

## Results

The benchmark harness runs each implementation three times and reports the
minimum. The table uses a stable representative run of the final 100-task
configuration. Each task result was checked against every pixel of the serial
output.

| View | Serial | ISPC, one core | ISPC + tasks | SIMD speedup | Total speedup | Tasks over ISPC |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 208.775 ms | 58.096 ms | 10.808 ms | 3.59x | **19.32x** | 5.38x |
| 2 | 112.309 ms | 39.948 ms | 7.586 ms | 2.81x | **14.81x** | 5.27x |

The 3.59x single-core result on View 1 is close to the four-FP32-lane width of
Neon. The multicore layer contributes another 5.38x rather than 8x because the
M1 combines four performance cores with four slower efficiency cores, and the
runtime still pays task scheduling, synchronization, and residual load-balance
costs.

## Why `foreach` and `launch` are separate

Although both constructs express independent work, they operate at different
granularities. `foreach` is a low-overhead compiler mechanism for mapping fine-
grained loop iterations to the lanes of one SIMD gang. `launch` creates coarse
tasks that a runtime schedules across worker threads and CPU cores. Each task
can then use `foreach`, composing multicore parallelism with SIMD parallelism.

An implementation could automatically distribute every `foreach` over all
cores, but that policy would often be counterproductive. Small or nested loops
could create more scheduling and synchronization work than useful computation;
automatic nested parallelism could oversubscribe the machine; and a compiler
cannot generally choose the best task size for cache locality and irregular
work at runtime. Keeping the mechanisms separate makes task creation explicit
while preserving predictable, inexpensive SIMD execution inside a task.

## Build and reproduce

The Makefile automatically selects `neon-i32x8` on Apple Silicon and
`avx2-i32x8` on x86-64. The repository-local ISPC archive is preferred when it
is present.

```bash
cd prog3_mandelbrot_ispc
make clean
make

./mandelbrot_ispc --tasks
./mandelbrot_ispc --tasks --view 2
```

To repeat the task-count experiment, clean before changing the build variable
because Make does not track command-line variable changes as dependencies:

```bash
make clean
make ISPC_TASK_COUNT=40
./mandelbrot_ispc --tasks
```

References: [ISPC User's Guide](https://ispc.github.io/ispc.html),
[Arm Coding for Neon](https://developer.arm.com/documentation/102159/latest/),
and [Apple M1 overview](https://www.apple.com/newsroom/2020/11/apple-unleashes-m1/).
