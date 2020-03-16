#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
import pyopencl.cltypes

from opencl import Mem, run_kernel


def run(ctx, src):
    n = 64
    s = np.arange(n, dtype=cl.cltypes.int)
    d = np.zeros_like(s)
    run_kernel(ctx, src, (n,), Mem(d), Mem(s))
    assert np.allclose(d, s)
    return (d, s)
