#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from opencl import Mem, run_kernel


def _run(ctx, src):
    n = 64
    a = 255*np.clip(np.arange(n, dtype=cltypes.uchar) % 2, 0, 1)
    b = 255*np.clip((np.arange(n, dtype=cltypes.uchar) + 1) % 2, 0, 1)
    c = 255*np.clip((np.arange(n, dtype=cltypes.uchar)//2) % 2, 0, 1)
    buf = [a, b, c]
    
    for i in range(23):
        buf.append(np.zeros(n, dtype=cltypes.uchar))
    
    for i in range(18):
        buf.append(255*np.clip(np.arange(n, dtype=cltypes.uchar) % 3, 0, 1))

    run_kernel(ctx, src, (n,), *[Mem(x) for x in buf])

    buf = [b & 1 for b in buf]
    return buf
