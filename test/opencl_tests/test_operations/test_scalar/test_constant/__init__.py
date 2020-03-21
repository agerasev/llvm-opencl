#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from opencl import Mem, run_kernel


def run(ctx, src):
    m, n = 8, 64
    a = np.zeros(n, dtype=cltypes.int)
    b = np.zeros(n, dtype=cltypes.float)
    run_kernel(ctx, src, (n,), *[Mem(x) for x in (a, b)])
    return (a, b)
