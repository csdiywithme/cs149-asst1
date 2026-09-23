# Iterative Square Root: SIMD Divergence and Multicore Scaling

This experiment studies how data-dependent convergence affects SIMD and
multicore performance. The kernel computes the square root of 20 million
single-precision values using an iterative reciprocal-square-root update. By
changing only the input distribution, the same ISPC program ranges from a
10.22x single-core speedup to a 0.89x slowdown relative to scalar code.

## Platform and measurement

- Apple M1: four performance cores and four efficiency cores
- Native arm64 C++ build with `-O3 -march=native`
- ISPC 1.31 targeting `neon-i32x8`
- 20,000,000 inputs, initial guess `1.0f`, error threshold `1e-5`
- 64 ISPC tasks in the multicore implementation
- Three trials per implementation; the harness reports the minimum
- Every result checked against `std::sqrt` with a `1e-4` tolerance

Arm Neon registers are 128 bits wide and hold four FP32 values. The ISPC
target nevertheless has a gang size of eight, so `programCount` is eight and
an eight-instance gang is lowered to multiple Neon operations. Input patterns
in the divergence experiments therefore repeat every eight elements even
though the physical FP32 vector width is four.

The three reported speedups have different meanings:

```text
SIMD speedup       = serial time / ISPC time
Multicore benefit  = ISPC time / task-ISPC time
Total speedup      = serial time / task-ISPC time

Total speedup = SIMD speedup * multicore benefit
```

The program prints SIMD speedup and total task speedup. Multicore benefit is
computed separately so that poor SIMD utilization is not confused with poor
task scaling.

## Algorithm and source of divergence

For input `S`, the kernel iteratively refines an estimate of `1/sqrt(S)`:

```text
error = abs(guess * guess * S - 1)
guess = (3 * guess - S * guess^3) / 2
result = S * guess
```

With the initial guess fixed at one, values near one converge quickly, while
values near the upper boundary at three require many more iterations. In
scalar code, each input exits its own loop immediately after convergence. In
ISPC, the eight lanes of a gang execute together under a mask. Finished lanes
become inactive, but the gang continues until its slowest lane converges.

## Complete experiment summary

Times below are representative stable runs. Repeated runs were within a few
percent unless otherwise noted. The serial time of the starter distribution
is approximate because that line was truncated in the captured terminal log;
it is reconstructed from both printed speedups.

| Input construction | Serial | ISPC | Task ISPC | SIMD | Multicore | Total |
|---|---:|---:|---:|---:|---:|---:|
| Starter random `[0.001, 2.999]` | ~924 ms | 201.410 ms | 34.946 ms | 4.59x | 5.76x | 26.43x |
| Random `[1.501, 1.601]` | 99.075 ms | 36.534 ms | 7.941 ms | 2.71x | 4.60x | 12.48x |
| Random `[2.8501, 2.9501]` | 1160.332 ms | 176.107 ms | 32.714 ms | 6.59x | 5.38x | 35.47x |
| Random `[1.0, 1.1]` | 204.783 ms | 28.857 ms | 6.057 ms | 7.10x | 4.76x | 33.81x |
| Centered random `[0.95, 1.05]` | 68.724 ms | 16.756 ms | 3.709 ms | 4.10x | 4.52x | 18.53x |
| Random `[1.0, 1.01]` | 171.554 ms | 16.792 ms | 3.819 ms | **10.22x** | 4.40x | **44.92x** |
| Random `[1.0, 1.001]` | 27.919 ms | 9.257 ms | 3.390 ms | 3.02x | 2.73x | 8.24x |
| Every 4: one `[2.9,2.901]`, three `[1,1.001]` | 133.728 ms | 139.527 ms | 37.669 ms | 0.96x | 3.70x | 3.55x |
| Every 8: one `[2.9989,2.9999]`, seven exact `1` | 401.621 ms | 449.270 ms | 83.550 ms | **0.89x** | 5.38x | 4.81x |
| Every 4: one `[2.9989,2.9999]`, three exact `1` | 788.608 ms | 460.724 ms | 83.019 ms | 1.71x | 5.55x | 9.50x |

## Baseline: random values over the full valid range

The starter input spans almost the entire valid interval. It mixes values that
converge immediately with values that require many iterations, so adjacent
lanes diverge. Despite that divergence, the computation is heavy enough to
amortize vector control and memory overhead, producing a 4.59x SIMD speedup.
The task version adds another 5.76x through multicore execution, for 26.43x
end-to-end speedup.

## Controlled convergence ranges

A numerical sampling of the update rule gives the following approximate
iteration distributions. These counts explain the major timing changes; exact
boundaries can move slightly under FP32 rounding.

| Input range | Approximate convergence behavior |
|---|---|
| `[0.95,1.05]` | 7.4% take one iteration; 92.6% take two |
| `[1,1.1]` | 3.6% take one; 67.0% take two; 29.4% take three |
| `[1,1.01]` | 0.1% take zero; 36.3% take one; 63.6% take two |
| `[1,1.001]` | 1.0% take zero; 99.0% take one |
| `[1.501,1.601]` | Nearly all take four iterations |
| `[2.8501,2.9501]` | Nearly all take 10-13 iterations |

### Uniform work is not automatically the largest measured speedup

The centered interval `[0.95,1.05]` makes almost every element execute two
iterations. SIMD utilization is high, but the scalar loop is also regular and
easy to predict. Its 4.10x result is close to the physical four-FP32-lane width
of Neon and is a useful controlled example of straightforward SIMD scaling.

The `[1.501,1.601]` input is even more uniform: nearly all values take four
iterations. It eliminates most lane divergence, but it also gives scalar code
a predictable loop and leaves the ISPC implementation's mask and eight-wide
lowering overhead visible. The observed SIMD speedup was 2.71x.

The range `[2.8501,2.9501]` performs much more arithmetic. This amortizes fixed
overhead and improves both vector and task scaling, but different lanes still
finish between roughly 10 and 13 iterations. The result was 6.59x SIMD and
35.47x total speedup.

### Why `[1,1.01]` produced the largest relative speedup

The best measured relative speedup came from random values in `[1,1.01]`, not
from the distribution with the greatest raw arithmetic cost. About 36% of
values take one iteration and 64% take two. In an eight-lane gang, the
probability that at least one lane needs the second iteration is approximately

```text
1 - 0.36^8 > 99.9%
```

so almost every ISPC gang executes a predictable two iterations. The second
iteration still has about 64% useful lanes, which is adequate utilization.

The scalar loop, however, sees a random per-element sequence of one- and two-
iteration exits. The most plausible explanation for its unusually high
171.554 ms time is branch misprediction at this discrete convergence boundary.
ISPC converts those per-element exit branches into gang-level mask updates.
This interpretation is an inference from the timing and iteration
distribution rather than a direct hardware-counter measurement, but the
10.22x result--greater than both the four-wide hardware vector and eight-wide
ISPC gang sizes--shows that the gain cannot come from lane width alone. Part of
the speedup must come from making the scalar baseline relatively less efficient.

The wider `[1,1.1]` interval usually forces a third gang iteration, but only
about 29% of lanes need it. That low-utilization final iteration reduces SIMD
speedup to 7.10x. The very narrow `[1,1.001]` interval has almost no divergence,
but nearly every value needs only one iteration. The scalar implementation
finishes in 27.919 ms, while fixed vector, memory, and task overheads dominate;
SIMD speedup falls to 3.02x and multicore benefit to 2.73x.

An additional validation experiment would sort the same `[1,1.01]` inputs
before timing. Sorting would cluster equal convergence counts, improve scalar
branch predictability, and group similar lanes. If serial time falls much more
than ISPC time, it would directly support the branch-prediction explanation.

## Constructing a worst case for SIMD

The first adverse layout repeated every four elements:

```text
[H E E E | H E E E]
```

Here `H` is random in `[2.9,2.901]` and needs about 11 iterations, while `E`
is random in `[1,1.001]` and almost always needs one. Because the ISPC gang has
eight lanes, two lanes remain active during the long tail. Effective late-loop
utilization is about 25%, and ISPC becomes slightly slower than scalar at
0.96x.

The stronger worst case aligns its period with the logical gang width:

```text
[H E E E E E E E]
```

`E` is exactly one, so its initial error is zero and it never enters the loop.
`H` lies in `[2.9989,2.9999]` and takes roughly 22-28 iterations. After the
initial predicate, only one of eight lanes remains active, giving 12.5% useful
lane occupancy during the long tail. The scalar program performs that long
loop for one element per eight; ISPC issues masked gang operations until the
single hard lane converges. ISPC therefore takes 449.270 ms versus 401.621 ms
for scalar, a 0.89x speedup--a 12% slowdown.

Using the same hard and easy values every four elements gives two hard lanes
per logical gang:

```text
[H E E E | H E E E]
```

The scalar program now processes twice as many hard inputs, so its time nearly
doubles from 401.621 to 788.608 ms. ISPC time changes only from 449.270 to
460.724 ms: both layouts already force every gang to run until a hard lane
finishes, and adding a second active lane does not double the number of vector
iterations. SIMD utilization doubles from 12.5% to 25%, so relative speedup
rises from 0.89x to 1.71x.

This comparison also exposes the distinction between logical and physical
width. `neon-i32x8` creates eight ISPC instances, but M1 Neon processes four
FP32 values per 128-bit vector. The eight-wide gang is lowered to two physical
vector halves. In the period-four layout, each half contains one hard lane. In
the period-eight layout, only one half contains the hard lane, yet masked gang
execution and loop control still keep the absolute ISPC time close to the
period-four case.

## Why task scaling improves for the heavier worst case

The mild adverse input obtains only 3.70x additional multicore speedup, while
the heavier period-eight case obtains 5.38x even though its SIMD result is
worse. There is no contradiction: the two levels of parallelism are separate.

```text
Mild case:   0.96x SIMD * 3.70x multicore = 3.55x total
Heavy case:  0.89x SIMD * 5.38x multicore = 4.81x total
```

Moving the hard value from approximately 2.9 to almost 3 substantially
increases arithmetic per task. Fixed task-launch, scheduling, synchronization,
and memory costs become a smaller fraction of runtime, so multicore scaling
approaches the aggregate throughput of the M1's heterogeneous cores. The
periodic layout also gives all 64 tasks nearly identical numbers of hard
values, preserving excellent inter-task load balance even though every task
has poor SIMD lane utilization.

The larger total speedup does not mean the heavy task version finishes sooner:
task time rises from 37.669 to 83.550 ms. Its speedup increases because serial
time grows much more, from 133.728 to 401.621 ms.

## Conclusions

- SIMD performance depends on the distribution of work within each gang, not
  just total arithmetic.
- Uniform convergence keeps lanes active, but also makes scalar branches easy
  to predict; maximum measured relative speedup need not equal maximum SIMD
  utilization.
- Tiny workloads are dominated by fixed vector, memory, and task overhead.
- A single long-running lane can reduce useful gang occupancy to 12.5% and
  make ISPC slower than scalar code.
- Multicore task scaling can remain strong even when SIMD scaling is poor, as
  long as tasks are balanced and contain enough computation.
- Software gang width (`x8`) and physical Neon FP32 width (four) are related
  but distinct concepts.

## Build

The Makefile automatically selects `neon-i32x8` on Apple Silicon and
`avx2-i32x8` on x86-64, preferring the repository-local ISPC archive.

```bash
cd prog4_sqrt
make clean
make
./sqrt
```

Only the input initialization in `main.cpp` changes between experiments. A
clean rebuild is required only when changing the ISPC target; modifying
`main.cpp` causes Make to rebuild and relink the C++ program automatically.

References: [ISPC User's Guide](https://ispc.github.io/ispc.html),
[Arm Neon Intrinsics Reference](https://arm-software.github.io/acle/neon_intrinsics/advsimd.html),
and [Apple M1 overview](https://www.apple.com/newsroom/2020/11/apple-unleashes-m1/).
