#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
import pyopencl.cltypes

from opencl import Mem, run_kernel

cl_int = cl.cltypes.int
cl_float = cl.cltypes.float

def run(ctx, src):
    n = 64
    b = np.arange(4*n, dtype=cl_float)/(2*n**0.5)
    a = np.zeros(4*n, dtype=cl_int)
    run_kernel(ctx, src, (n,), *[Mem(x) for x in [a, b]])
    return (a, b)
