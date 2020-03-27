#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.translate import translate
from test.cases.tester import Tester as BaseTester

class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src="source.clpp")
        self.n = 64

    def translate(self, src, **kws):
        return translate(
            src, suffix=self.suffix(**kws),
            fe={"opt": kws["opt"], "std": "clc++"},
        )

    def makeref(self):
        return (
            np.arange(self.n, dtype=cltypes.int),
            np.arange(self.n, dtype=cltypes.int),
        )

    def run(self, src, **kws):
        b = np.zeros(self.n, dtype=cltypes.int)
        a = np.zeros(self.n, dtype=cltypes.int)
        run_kernel(self.ctx, src, (self.n,), *[Mem(x) for x in [a, b]])
        return (a, b)
