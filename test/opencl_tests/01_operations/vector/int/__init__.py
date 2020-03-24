#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from opencl import Mem, run_kernel


def run(ctx, src):
    m = 256
    n = 4*m
    a = (np.arange(n, dtype=cltypes.int) % 51) - 51//2
    b = (np.arange(n, dtype=cltypes.int) % 67) - 67//2
    c = (np.arange(n, dtype=cltypes.int) % 19) - 19//2
    buf = [a, b, c]
    
    for i in range(23):
        buf.append(np.zeros(n, dtype=cltypes.int))
    
    for i in range(18):
        buf.append((np.arange(n, dtype=cltypes.int) % 29) - 29//2)

    run_kernel(ctx, src, (m,), *[Mem(x) for x in buf])
    return buf
