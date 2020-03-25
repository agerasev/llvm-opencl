#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from opencl import Mem, run_kernel

def run(ctx, src):
    n = 64
    a = np.arange(n, dtype=cltypes.int)
    b = np.zeros_like(a)
    run_kernel(ctx, src, (n,), *[Mem(x) for x in [a, b]])
    return (a, b)
