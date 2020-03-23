#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from opencl import Mem, run_kernel


def run(ctx, src):
    n = 8
    a = np.clip(np.arange(n, dtype=cltypes.uchar) % 2, 0, 1)
    b = np.clip((np.arange(n, dtype=cltypes.uchar) - 1) % 2, 0, 1)
    c = np.clip((np.arange(n, dtype=cltypes.uchar)//2) % 2, 0, 1)
    buf = [a, b, c]
    
    for i in range(23):
        buf.append(np.zeros(n, dtype=cltypes.uchar))
    
    for i in range(18):
        buf.append(np.clip(np.arange(n, dtype=cltypes.uchar) % 3, 0, 1))

    run_kernel(ctx, src, (n,), *[Mem(x) for x in buf])

    return buf
