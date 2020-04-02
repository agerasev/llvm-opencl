#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.cases.tester import Tester as BaseTester

skip = True

class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src="source.cl")

    def run(self, src, **kws):
        n = 64
        buf = [
            np.arange(n, dtype=cltypes.char),
            np.arange(n, dtype=cltypes.short),
            np.arange(n, dtype=cltypes.int),
            np.arange(n, dtype=cltypes.long),
            np.zeros(n*15, dtype=cltypes.uchar),
        ]
        run_kernel(self.ctx, src, (n,), *[Mem(x) for x in buf])
        return buf
