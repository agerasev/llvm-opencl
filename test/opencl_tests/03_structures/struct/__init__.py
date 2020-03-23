#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
import pyopencl.cltypes

from opencl import Mem, run_kernel

cl_int = cl.cltypes.int
cl_float = cl.cltypes.float

def run(ctx, src):
    n = 64
    a = np.arange(n, dtype=cl_int)
    b = np.pi*np.arange(n, dtype=cl_float)
    c = np.zeros_like(b)
    run_kernel(ctx, src, (n,), *[Mem(x) for x in [a, b, c]])
    return (a, b, c)
