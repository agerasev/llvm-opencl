#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.cases.tester import Tester as BaseTester


class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src="source.ll")
        self.n = 64
        self.ibuf = [
            np.arange(4*self.n, dtype=cltypes.int),
            -np.arange(4*self.n, dtype=cltypes.int),
            np.arange(self.n, dtype=cltypes.int) % 2,
            (np.arange(4*self.n, dtype=cltypes.int) % 3) & 1,
        ]
        self.obuf = [
            np.select(
                [np.repeat(self.ibuf[2], 4) > 0],
                [self.ibuf[0]], self.ibuf[1]
            ),
            np.select(
                [self.ibuf[3] > 0],
                [self.ibuf[0]], self.ibuf[1]
            ),
        ]

    def makeref(self):
        return self.ibuf + self.obuf

    def run(self, src, **kws):
        buf = self.ibuf + [np.zeros_like(x) for x in self.obuf]

        run_kernel(self.ctx, src, (self.n,), *[Mem(x) for x in buf])

        return buf
