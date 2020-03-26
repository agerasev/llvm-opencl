#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
import pyopencl.cltypes

from test.opencl import Mem, run_kernel

cl_float = cl.cltypes.float

def run(ctx, src):
    n = 64
    a = np.pi*np.arange(2*n, dtype=cl_float)/n
    b = np.e*np.arange(3*n, dtype=cl_float)/n
    c = np.zeros(4*n, dtype=cl_float)
    run_kernel(ctx, src, (n,), *[Mem(x) for x in [c, a, b]])
    return (c, a, b)
