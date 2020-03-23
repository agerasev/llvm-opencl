#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from opencl import Mem, run_kernel


def run(ctx, src):
    n = 64
    #b = np.arange(4*n, dtype=cltypes.float)/(2*n**0.5)
    ibuf = [
        np.arange(4*n, dtype=cltypes.uint) - (4*n)//2,
        np.arange(4*n, dtype=cltypes.int) - (4*n)//2,
        np.arange(4*n, dtype=cltypes.int) - (4*n)//2,
        np.arange(4*n, dtype=cltypes.float) - (4*n)//2,
    ]
    obuf = [
        np.zeros(4*n, dtype=cltypes.int),
        np.zeros(4*n, dtype=cltypes.uint),
        np.zeros(4*n, dtype=cltypes.float),
        np.zeros(4*n, dtype=cltypes.int),
    ]
    run_kernel(ctx, src, (n,), *[Mem(x) for x in ibuf + obuf])
    return ibuf + obuf
