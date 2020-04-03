#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.cases.tester import Tester as BaseTester


class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src="source.ll")
        self.a = np.array([0, -1, -1, 1<<15, 1<<16], dtype=cltypes.uint)
        self.b = np.array([0, 1, -1, 1<<16, 1<<16], dtype=cltypes.uint)
        self.c = self.a*self.b
        self.o = np.array([0, 0, 1, 0, 1], dtype=cltypes.uchar)
        self.n = len(self.a)

    def makeref(self):
        return [self.a, self.b, self.c, self.o]

    def run(self, src, **kws):
        a = self.a
        b = self.b
        c = np.zeros_like(self.c)
        o = np.zeros_like(self.o)

        run_kernel(self.ctx, src, (self.n,), *[Mem(x) for x in [a, b, c, o]])

        return [a, b, c, o]
