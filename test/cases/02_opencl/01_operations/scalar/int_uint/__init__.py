#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.cases.tester import Tester as BaseTester


class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src="source.cl")

    def run(self, src, **kws):
        n = 64
        a = np.arange(n, dtype=cltypes.int) - n//2
        b = np.arange(n, dtype=cltypes.int)
        c = np.zeros(n, dtype=cltypes.float)
        d = np.zeros(n, dtype=cltypes.int)
        run_kernel(self.ctx, src, (n,), *[Mem(x) for x in [a, b, c, d]])
        return (a, b, c, d)
