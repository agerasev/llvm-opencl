#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from opencl import Mem, run_kernel


def run(ctx, src):
    m = 256
    n = 4*m
    a = (np.arange(n, dtype=cltypes.uint) % 51)
    b = (np.arange(n, dtype=cltypes.uint) % 67)
    c = (np.arange(n, dtype=cltypes.uint) % 19)
    buf = [a, b, c]
    
    for i in range(23):
        buf.append(np.zeros(n, dtype=cltypes.uint))
    
    for i in range(18):
        buf.append((np.arange(n, dtype=cltypes.uint) % 29))

    run_kernel(ctx, src, (m,), *[Mem(x) for x in buf])
    return buf
