#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.translate import translate
from test.cases.tester import Tester as BaseTester

class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src="source.cpp.cl")
        self.n = 64

        self.output = [
            np.arange(self.n, dtype=cltypes.int),
            np.arange(self.n, dtype=cltypes.float),
            np.arange(self.n, dtype=cltypes.int)**2,
            np.arange(self.n, dtype=cltypes.float)**2,
        ]
        self.input = [
            self.output[0].copy(),
            self.output[1].copy(),
            np.arange(self.n, dtype=cltypes.int)**2,
            np.arange(self.n, dtype=cltypes.float)**2,
        ]

    def translate(self, src, **kws):
        return translate(
            src, suffix=self.suffix(**kws),
            fe={"opt": kws["opt"], "std": "clc++"},
        )

    def makeref(self):
        return self.output

    def run(self, src, **kws):
        buffers = [b.copy() for b in self.input]
        run_kernel(self.ctx, src, (self.n,), *[Mem(x) for x in buffers])
        return buffers
