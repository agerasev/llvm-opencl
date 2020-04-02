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
        self.a = np.arange(4*self.n, dtype=cltypes.float)
        self.b = 2*np.arange(2*self.n, dtype=cltypes.float)

    def makeref(self):
        return [self.a, self.b]

    def run(self, src, **kws):
        a = self.a
        b = np.zeros_like(self.b)

        run_kernel(self.ctx, src, (self.n,), *[Mem(x) for x in [a, b]])

        return [a, b]
