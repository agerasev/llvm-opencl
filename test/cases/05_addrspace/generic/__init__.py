#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.translate import translate
from test.opencl import Mem, run_kernel
from test.cases.tester import Tester as BaseTester


class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src="source.cl")
        self.n = 64
        self.a = np.arange(self.n, dtype=cltypes.int)

    def makeref(self):
        b = self.a.copy()
        b[1::2] *= -1
        return (self.a.copy(), b)

    def translate(self, src, **kws):
        return translate(
            src, suffix=self.suffix(**kws),
            fe={"opt": kws["opt"], "std": "cl2.0"},
        )

    def run(self, src, **kws):
        a = self.a.copy()
        b = np.zeros_like(a)
        run_kernel(self.ctx, src, (self.n,), *[Mem(x) for x in [a, b]])
        print(a, b)
        return (a, b)
