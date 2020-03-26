#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
import pyopencl.cltypes

from test.opencl import Mem, run_kernel


def run(ctx, src):
    n = 64
    a = np.arange(n, dtype=cl.cltypes.int)
    b = np.arange(n, dtype=cl.cltypes.float)
    c = np.zeros_like(a)
    d = np.zeros_like(b)
    run_kernel(ctx, src, (n,), *[Mem(x) for x in (a, b, c, d)])
    assert np.allclose(a, c)
    assert np.allclose(b, d)
    return (a, b, c, d)
