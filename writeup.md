# Parallel Mandelbrot Renderer with Static Load Balancing

See also [Mandelbrot with ISPC SIMD and Tasks](prog3_mandelbrot_ispc/README.md)
for the Program 3 multicore SIMD implementation and measurements.

The [iterative square-root SIMD study](prog4_sqrt/README.md) documents Program
4's best- and worst-case input experiments on Apple M1.

This project parallelizes a Mandelbrot renderer with C++ threads and studies
how workload decomposition and heterogeneous CPU cores affect scaling. The
final implementation reaches **3.74x speedup with four threads** and up to
**5.96x with eight threads** on an Apple M1, while preserving bit-for-bit
output equivalence with the serial renderer.

## Problem

The renderer maps a `1600 x 1200` image to the complex plane. Each pixel runs
the Mandelbrot recurrence for up to 256 iterations, but pixels can escape at
very different iterations. As a result, equal numbers of pixels do not imply
equal amounts of computation.

A first decomposition into contiguous horizontal bands exposed this problem:
threads assigned to expensive regions finished later and determined the total
runtime. Adding threads alone could not fix the critical-path imbalance.

## Design

The optimized renderer uses deterministic, static, cyclic scheduling:

1. Divide the image into full-width, one-row chunks.
2. Assign chunk `i` to worker `i % numThreads`.
3. Let each worker process its assigned chunks independently.
4. Join all workers before returning to the caller.

This interleaves spatially distant rows across workers, distributing both
high-iteration and low-iteration regions without a shared queue, locks, or
atomics. The implementation also keeps the original global image dimensions
and complex-plane coordinates for every chunk, so each worker writes directly
to a disjoint region of the output buffer.

The last chunk is bounded with ceiling division and `std::min`, making the
partition correct even when the image height is not divisible by the chunk
size or thread count. One-row chunks add function-call overhead, but the work
per row is large enough that the improved load balance dominates that cost.

The caller launches `numThreads - 1` new `std::thread` objects and uses the
main application thread as worker 0, avoiding one unnecessary worker creation.

## Measurement Methodology

The program is compiled with Apple Clang 17 using `-O3` and measures wall-clock
time with `CycleTimer::currentSeconds()`. Each reported serial and parallel
time is the minimum of five trials, matching the supplied benchmark harness.

Correctness is checked after every benchmark by comparing every output pixel
against the serial result. A mismatch causes the process to exit with an
error.

Per-thread measurements are collected in a separate untimed profiling run:

```bash
./mandelbrot -t 4 --view 1 --profile
```

Each worker records one elapsed-time value in its own argument structure. The
main thread prints these values only after all joins. This avoids timing
distortion from concurrent `printf` calls, stream locking, terminal rendering,
and competition with workers that are still computing.

### Test platform

- Apple M1
- 4 performance cores and 4 efficiency cores
- 8 physical/logical CPU cores, with no simultaneous multithreading
- macOS, native arm64 executable

## Results

The following measurements were collected from the final one-row cyclic
decomposition. Times are in milliseconds.

| Threads | View 1 time | View 1 speedup | View 2 time | View 2 speedup |
|---:|---:|---:|---:|---:|
| 1 | 412.872 | 1.02x | 218.549 | 1.00x |
| 2 | 213.108 | 1.93x | 113.117 | 1.93x |
| 3 | 147.028 | 2.80x | 78.230 | 2.79x |
| 4 | 110.897 | 3.74x | 58.787 | 3.72x |
| 8 | 69.056 | 5.96x | 39.543 | 5.52x |
| 16 | 72.273 | 5.70x | 38.805 | 5.63x |
| 24 | 70.392 | 5.85x | 37.402 | 5.85x |

Small differences above eight threads are run-to-run variation rather than
sustained scaling. Repeated runs remain in approximately the same 5.5x–6.0x
performance range.

### Per-thread profile

On View 1 with four threads, a separate profiling run produced:

| Worker | Runtime |
|---:|---:|
| 0 | 110.511 ms |
| 1 | 110.895 ms |
| 2 | 110.270 ms |
| 3 | 110.300 ms |

The range is only 0.625 ms, less than 1% of the mean runtime, confirming that
the cyclic decomposition distributes this workload evenly across four
workers.

With eight threads, individual times ranged from 62.343 ms to 77.879 ms. At
that point equal work does not guarantee equal completion time because the M1
contains two classes of cores with different throughput, and the operating
system controls thread placement and migration.

## Scaling Analysis

Scaling is close to linear through the four performance cores: four threads
achieve 3.74x speedup on View 1 and 3.72x on View 2. Moving from four to eight
threads improves View 1 throughput by about 59%, but does not double it. The
additional threads can use the four efficiency cores, whose per-core
throughput is lower than that of the performance cores.

The M1 has eight physical cores and no simultaneous multithreading. Running 16
or 24 compute-bound threads therefore adds software threads but no execution
resources. Threads time-slice on already-saturated cores and introduce
scheduling overhead, so performance plateaus around the eight-thread result.

## Key Takeaways

- Work units must be balanced by computational cost, not only by element
  count.
- Fine-grained static scheduling can approximate load balancing without the
  synchronization cost and nondeterminism of a dynamic work queue.
- Instrumentation can perturb parallel measurements; diagnostic output should
  be kept outside the timed benchmark path.
- Heterogeneous cores make thread count an incomplete model of available CPU
  throughput.
- Oversubscribing a compute-bound workload does not help when all physical
  cores are already occupied.

## Build and Reproduce

```bash
cd prog1_mandelbrot_threads
make

# Benchmark the two image regions.
./mandelbrot --threads 4 --view 1
./mandelbrot --threads 8 --view 2

# Collect per-thread timings in a separate diagnostic run.
./mandelbrot --threads 8 --view 1 --profile
```
