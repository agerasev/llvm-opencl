#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.cases.tester import Tester as BaseTester


class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src="source.cl")

    def translate(self, src, **kws):
        return super().translate(src, **kws, debug=True)

    def run(self, src, **kws):
        n = 8
        a = np.arange(n, dtype=cltypes.int)
        b = np.zeros_like(a)
        run_kernel(self.ctx, src, (n,), Mem(a), Mem(b))
        return (a, b)
