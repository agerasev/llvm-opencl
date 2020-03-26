#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
import pyopencl.cltypes

from test.opencl import Mem, run_kernel

def run(ctx, src):
    n = 64
    a = np.arange(5*n, dtype=cl.cltypes.int)
    b = np.arange(n, dtype=cl.cltypes.int)
    run_kernel(ctx, src, (n,), *[Mem(x) for x in [a, b]])
    return (a, b)
