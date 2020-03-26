#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel


def run(ctx, src):
    n = 64
    a = np.arange(4*n, dtype=cltypes.int)
    b = np.arange(4*n, dtype=cltypes.int) - 4*n
    c = (np.arange(4*n, dtype=cltypes.int) % 2) - 1
    d = np.zeros(4*n, dtype=cltypes.int)
    run_kernel(ctx, src, (n,), *[Mem(x) for x in [a, b, c, d]])
    return (a, b, c, d)
