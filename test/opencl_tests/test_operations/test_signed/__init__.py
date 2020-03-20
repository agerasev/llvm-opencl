#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
import pyopencl.cltypes

from opencl import Mem, run_kernel


def run(ctx, src):
    n = 64
    c = np.arange(n, dtype=cl.cltypes.int) - n//2
    d = np.arange(n, dtype=cl.cltypes.uint)
    a = np.zeros_like(c)
    b = np.zeros_like(d)
    run_kernel(ctx, src, (n,), *[Mem(x) for x in [a, b, c, d]])
    return (a, b, c, d)
