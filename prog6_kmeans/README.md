# K-Means: Profiling and Parallel Assignment

This program clusters one million 100-dimensional points into three clusters.
The optimization process first profiles the three phases of each K-means
iteration, then parallelizes only `computeAssignments`, as required by the
assignment. The final four-thread implementation preserves the reference output
exactly and reduces total runtime from 14.001 seconds to 5.690 seconds in a
controlled comparison, a 2.46x speedup on an Apple M1.

## Platform and data

- Apple M1: four performance cores and four efficiency cores
- Native arm64 C++ build with `-O3 -march=native`
- Four assignment workers: three `std::thread` objects plus the main thread
- Locally generated, starter-compatible dataset
- `M = 1,000,000`, `N = 100`, `K = 3`, and `epsilon = 0.1`

The local dataset has the same dimensions and binary layout as the course data,
but it is not the Stanford AFS file. Absolute timings can therefore be compared
within this experiment, not directly with measurements from the myth machines.

## Original algorithm

Each K-means iteration performs three phases:

1. `computeAssignments` finds the closest centroid for every point.
2. `computeCentroids` averages the points assigned to each cluster.
3. `computeCost` sums the point-to-centroid distances for the convergence test.

For `M` points, `N` dimensions, and `K` clusters, assignment performs
`O(MKN)` work. Centroid and cost computation each perform `O(MN)` work. Here
`M` is one million, `N` is 100, and `K` is only three, so the assignment phase
offers a large number of independent point-level work units.

## Profiling and experiment progression

I added timers around assignment, centroid update, and cost computation. Times
below are representative wall-clock measurements on the same machine and local
dataset. Final four-thread runs ranged from 5.69 to 6.83 seconds because of
normal scheduling and system-state variation.

| Version | Assignment | Centroids | Cost | Total |
|---|---:|---:|---:|---:|
| Starter reference | -- | -- | -- | 14.001 s |
| First incorrect four-thread attempt | 12.964 s | 1.035 s | 2.671 s | 16.671 s |
| Disjoint point blocks with per-thread arrays | 3.532 s | 1.080 s | 2.689 s | 7.301 s |
| Final worker logic, one thread | 8.022 s | 1.071 s | 2.672 s | 11.765 s |
| Final worker logic, four threads | **2.167 s** | 0.898 s | 2.624 s | **5.690 s** |

The one-thread final worker measurement shows that assignment still accounts
for about 68% of its total runtime, making it the appropriate phase to
parallelize. The final controlled reference comparison gives

```text
14.001 / 5.690 = 2.46x total speedup.
```

Across repeated final runs, the median was 6.147 seconds, giving a more
conservative speedup of roughly 2.28x. Both the median and controlled-pair
results exceed the assignment's target of about 2.1x.

## First attempt: threads without work decomposition

The first threaded version constructed four workers, but every worker received
the same `M`, `start`, and `end` values. The original assignment function still
looped from point zero through point `M`, so every worker repeated the entire
assignment calculation and concurrently wrote the same assignment array.

The total work grew from approximately `MKN` to `4MKN`. Running four copies on
four cores did not shorten the critical path, and the shared writes constituted
a C++ data race even though the workers usually computed the same values. The
16.671-second result was consequently no better than the serial program.

## Final decomposition

The corrected implementation partitions the point index `m`, not the centroid
index `k`. Worker `i` out of `T` owns the half-open interval

```cpp
pointStart = M * i / T;
pointEnd   = M * (i + 1) / T;
```

This formulation covers all points exactly once even when `M` is not divisible
by `T`. In contrast, computing `M / T` first and giving every worker that many
points would leave the remainder unprocessed.

Each worker conceptually executes:

```text
for each owned point m:
    best distance = infinity
    best cluster = none
    for each cluster k:
        compute distance(point m, centroid k)
        update the local best result
    assignment[m] = best cluster
```

All workers read the shared point and centroid arrays, but each writes a
disjoint section of `clusterAssignments`. No lock, atomic operation, or shared
reduction is required. The main thread handles worker zero, three additional
threads handle the remaining blocks, and `join` forms a barrier before the
serial centroid phase reads the completed assignments.

Partitioning points is preferable to partitioning centroids for this dataset:

- There are one million points but only three centroids, so the point dimension
  exposes much more parallelism.
- A point owner can select its closest centroid locally.
- Centroid-based workers would need to merge competing minimum distances and
  assignments for every point.

## Removing the per-thread distance array

An intermediate correct implementation preserved the original centroid-major
loop order and allocated an `M`-element `minDist` array in every worker. Each
worker used only its own quarter of that array, making the allocation much
larger than necessary.

The final point-major loop keeps only two local values for the current point:
its minimum distance and closest cluster. This removes all assignment-time heap
allocation. It also checks the three centroids while the current 800-byte point
is still cache-resident, instead of streaming over the entire 800 MB dataset
once for every centroid. This change explains why the final one-thread worker
is faster than the starter reference even before multicore execution is added.

The point-major loop does not change numerical semantics. Centroids are still
examined in increasing `k` order, and the original `dist` function is used
unchanged.

## Scaling limit

The assignment phase itself improves from 8.022 seconds with one worker to
2.167 seconds in the fastest controlled four-worker run, about 3.70x. It does
not reach a perfect 4x because of thread creation and synchronization, memory
traffic, scheduling, and ordinary measurement variation.

After assignment is accelerated, the serial centroid and cost phases occupy
approximately 3.52 of the final 5.69 seconds. They are now about 62% of total
runtime. Amdahl's law therefore explains why a near-4x assignment improvement
produces only about a 2x multicore improvement over the final one-thread worker
and a 2.3-2.5x end-to-end improvement over the starter. Only assignment is
parallelized, so the implementation remains within the problem constraint.

## Correctness validation

I built the unmodified repository `HEAD` in a separate temporary directory and
ran it on the same dataset, then ran the optimized version. Both `start.log` and
`end.log` compared byte-for-byte equal. The matching final-log SHA-256 was

```text
703dc4923572e7fab85db3e8a2f49b8a07424fe7a59592b43197933a8313ac03
```

Running `plot.py` also produced a coherent final visualization with three
separated clusters and one centroid in each cluster. The generated logs and
images are benchmark artifacts and are excluded from version control.

## Build and reproduce

```bash
cd prog6_kmeans
make clean
make
./kmeans
```

The supplied Matplotlib 3.6 requirement is not compatible with NumPy 2.x on
this Python environment. A temporary environment for plotting can be created
with an explicit NumPy upper bound:

```bash
python3 -m venv .venv
.venv/bin/pip install 'numpy<2' -r requirements.txt
.venv/bin/python plot.py
```

The dataset, virtual environment, logs, executable, objects, and generated
images should remain local and should not be committed.
