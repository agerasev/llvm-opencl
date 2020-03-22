#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from opencl import Mem, run_kernel


def run(ctx, src):
    n = 64
    a = np.pi*np.arange(n, dtype=cltypes.float)/n
    b = np.e*np.arange(n, dtype=cltypes.float)/n
    c = np.sqrt(2)*np.arange(n, dtype=cltypes.float)/n
    d = np.zeros(3*n, dtype=cltypes.float)
    run_kernel(ctx, src, (n,), *[Mem(x) for x in [a, b, c, d]])
    return (a, b, c, d)
