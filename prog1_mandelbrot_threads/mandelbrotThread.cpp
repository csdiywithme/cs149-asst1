#include <stdio.h>
#include <thread>
#include <algorithm>

#include "CycleTimer.h"

struct WorkerArgs {
    float x0, x1;
    float y0, y1;
    int width;
    int height;
    int maxIterations;
    int* output;
    int threadId;
    int numThreads;
    double interval;
};


extern void mandelbrotSerial(
    float x0, float y0, float x1, float y1,
    int width, int height,
    int startRow, int numRows,
    int maxIterations,
    int output[]);


//
// workerThreadStart --
//
// Thread entrypoint.
void workerThreadStart(WorkerArgs * const args) {
    static constexpr int ROWS_PER_CHUNK = 1;

    const double startTime = CycleTimer::currentSeconds();
    const int chunkCount =
        (args->height + ROWS_PER_CHUNK - 1) / ROWS_PER_CHUNK;

    // Assign full-width row chunks round-robin. This spreads expensive and
    // inexpensive regions of the image across workers without synchronization.
    for (int chunkId = args->threadId;
         chunkId < chunkCount;
         chunkId += args->numThreads) {
        const int startRow = chunkId * ROWS_PER_CHUNK;
        const int rowCount = std::min(ROWS_PER_CHUNK, args->height - startRow);

        mandelbrotSerial(args->x0, args->y0, args->x1, args->y1,
                        args->width, args->height,
                        startRow, rowCount,
                        args->maxIterations,
                        args->output);
    }

    args->interval = CycleTimer::currentSeconds() - startTime;
}

//
// MandelbrotThread --
//
// Multi-threaded implementation of mandelbrot set image generation.
// Threads of execution are created by spawning std::threads.
static void mandelbrotThreadImpl(
    int numThreads,
    float x0, float y0, float x1, float y1,
    int width, int height,
    int maxIterations, int output[],
    bool printProfile)
{
    static constexpr int MAX_THREADS = 32;

    if (numThreads < 1 || numThreads > MAX_THREADS)
    {
        fprintf(stderr, "Error: Thread count must be between 1 and %d\n",
                MAX_THREADS);
        exit(1);
    }

    // Creates thread objects that do not yet represent a thread.
    std::thread workers[MAX_THREADS];
    WorkerArgs args[MAX_THREADS];

    for (int i=0; i<numThreads; i++) {
      
        args[i].x0 = x0;
        args[i].y0 = y0;
        args[i].x1 = x1;
        args[i].y1 = y1;
        args[i].width = width;
        args[i].height = height;
        args[i].maxIterations = maxIterations;
        args[i].numThreads = numThreads;
        args[i].output = output;
      
        args[i].threadId = i;
    }

    // Spawn the worker threads.  Note that only numThreads-1 std::threads
    // are created and the main application thread is used as a worker
    // as well.
    for (int i=1; i<numThreads; i++) {
        workers[i] = std::thread(workerThreadStart, &args[i]);
    }
    
    workerThreadStart(&args[0]);

    // join worker threads
    for (int i=1; i<numThreads; i++) {
        workers[i].join();
    }
    if (printProfile) {
        for (int i = 0; i < numThreads; i++) {
            printf("thread %d: %.3f ms\n",
                   args[i].threadId,
                   args[i].interval * 1000);
        }
    }
}

void mandelbrotThread(
    int numThreads,
    float x0, float y0, float x1, float y1,
    int width, int height,
    int maxIterations, int output[])
{
    mandelbrotThreadImpl(numThreads, x0, y0, x1, y1,
                         width, height, maxIterations, output, false);
}

void mandelbrotThreadProfile(
    int numThreads,
    float x0, float y0, float x1, float y1,
    int width, int height,
    int maxIterations, int output[])
{
    mandelbrotThreadImpl(numThreads, x0, y0, x1, y1,
                         width, height, maxIterations, output, true);
}
