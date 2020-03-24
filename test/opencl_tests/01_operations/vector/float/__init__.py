#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from opencl import Mem, run_kernel


def run(ctx, src):
    m = 256
    n = 4*m
    a = np.pi*((np.arange(n, dtype=cltypes.float) % 51) - 51//2)
    b = np.e*((np.arange(n, dtype=cltypes.float) % 67) - 67//2)
    c = (np.arange(n, dtype=cltypes.float) % 19) - 19//2
    buf = [a, b, c]
    
    for i in range(13):
        buf.append(np.zeros(n, dtype=cltypes.float))
    
    for i in range(4):
        buf.append(np.sqrt(2)*((np.arange(n, dtype=cltypes.float) % 29) - 29//2))

    run_kernel(ctx, src, (m,), *[Mem(x) for x in buf])
    return buf
