#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
import pyopencl.cltypes

from opencl import Mem, run_kernel


def run(ctx, src):
    n = 64
    a = np.pi*np.arange(n, dtype=cl.cltypes.float)/n
    b = np.e*np.arange(n, dtype=cl.cltypes.float)
    c = np.zeros_like(a)
    run_kernel(ctx, src, (n,), *[Mem(x) for x in [c, a, b]])
    return (c, a, b)
