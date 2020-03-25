#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from opencl import Mem, run_kernel


def run(ctx, src):
    n = 256
    a = (np.arange(n, dtype=cltypes.int) % 13) - 13//2
    b = (np.arange(n, dtype=cltypes.int) % 17) - 17//2
    c = (np.arange(n, dtype=cltypes.int) % 5) - 5//2
    buf = [a, b, c]
    
    for i in range(23):
        buf.append(np.zeros(n, dtype=cltypes.int))
    
    for i in range(18):
        buf.append((np.arange(n, dtype=cltypes.int) % 7) - 7//2)

    run_kernel(ctx, src, (n,), *[Mem(x) for x in buf])
    return buf
