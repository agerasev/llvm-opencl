#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
import pyopencl.cltypes

from test.opencl import Mem, run_kernel

cl_int = cl.cltypes.int

def run(ctx, src):
    n = 64
    b = np.arange(n, dtype=cl_int)
    a = np.zeros_like(b)
    run_kernel(ctx, src, (n,), *[Mem(x) for x in [a, b]])
    return (a, b)
