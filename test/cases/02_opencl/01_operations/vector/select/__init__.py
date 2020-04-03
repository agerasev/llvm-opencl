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
        buf = [
            np.arange(4*n, dtype=cltypes.int),
            np.arange(4*n, dtype=cltypes.int) - 4*n,
            (np.arange(4*n, dtype=cltypes.int) % 2) - 1,
            np.zeros(4*n, dtype=cltypes.int),
            np.zeros(4*n, dtype=cltypes.int),
        ]
        run_kernel(self.ctx, src, (n,), *[Mem(x) for x in buf])
        return buf
