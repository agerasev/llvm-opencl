#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.translate import translate
from test.cases.tester import Tester as BaseTester

skip = True

class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src=("kernel.cl", "source.cpp"))
        self.n = 64

        self.a = np.arange(self.n, dtype=cltypes.int)**2
        self.b = np.arange(self.n, dtype=cltypes.int)
        self.c = self.a.copy()
        self.c[::2] *= 2
        self.c[1::2] **= 2

    def makeref(self):
        return [self.a, self.b, self.c]

    def run(self, src, **kws):
        buffers = [b.copy() for b in [self.a, self.b, np.zeros_like(self.c)]]
        run_kernel(self.ctx, src, (self.n,), *[Mem(x) for x in buffers])
        return buffers
