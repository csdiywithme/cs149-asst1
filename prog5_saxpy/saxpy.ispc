
export void saxpy_ispc(uniform int N,
                       uniform float scale,
                       uniform float X[],
                       uniform float Y[],
                       uniform float result[])
{
    foreach (i = 0 ... N) {           
        result[i] = scale * X[i] + Y[i];
    }
}
task void saxpy_ispc_task(uniform int N,
                          uniform int span,
                          uniform float scale,
                          uniform float X[],
                          uniform float Y[],
                          uniform float result[])
{
    uniform int indexStart = taskIndex * span;
    uniform int indexEnd = min(N, indexStart + span);

    // streaming_store writes one full SIMD gang, so keep the vectorized
    // region a multiple of programCount and handle the short tail normally.
    uniform int fullEnd =
        indexStart +
        ((indexEnd - indexStart) / programCount) * programCount;

    for (uniform int base = indexStart;
         base < fullEnd;
         base += programCount) {
        varying int i = base + programIndex;
        varying float value = scale * X[i] + Y[i];
#if USE_STREAMING_STORE
        streaming_store(&result[base], value);
#else
        result[i] = value;
#endif
    }

    foreach (i = fullEnd ... indexEnd) {
        result[i] = scale * X[i] + Y[i];
    }
}

export void saxpy_ispc_withtasks(uniform int N,
                                 uniform float scale,
                                 uniform float X[],
                                 uniform float Y[],
                                 uniform float result[])
{
    uniform int span = max(1, N / 64);

    uniform int numTasks = (N + span - 1) / span;

    launch[numTasks]
        saxpy_ispc_task(N, span, scale, X, Y, result);
}
