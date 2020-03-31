#!/usr/bin/env python3

import os
from subprocess import run

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.translate import check_rustc
from test.cases.tester import Tester as BaseTester

skip = True

class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src=("main.cl", "lib.rs"))
        self.n = 64
        self.a = np.arange(self.n, dtype=cltypes.int)

    def makeref(self):
        return (self.a.copy(), self.a**2)

    def run(self, src, **kws):
        n = 64
        a = self.a.copy()
        b = np.zeros_like(a)
        run_kernel(self.ctx, src, (self.n,), *[Mem(x) for x in [a, b]])
        return (a, b)

    def test_all(self, *args, **kwargs):
        if not check_rustc():
            raise Warning("unable to find `rustc`")
        super().test_all(*args, **kwargs)
