#!/usr/bin/env python3

import os
from subprocess import run

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.translate import gen_spir, gen_oclc, link
from test.cases.tester import Tester as BaseTester


class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src=("main.cl", "lib.c"))
        self.n = 64
        self.a = np.arange(self.n, dtype=cltypes.int)

    def link(self, srcs, **kws):
        return link(srcs, os.path.join(
            self.loc, "all.{}".format(srcs[0].split(".", 1)[1])
        ))

    def translate(self, srcs, **kws):
        opt = kws["opt"]
        irs = []
        for src in srcs:
            ir = "{}.o{}.gen.ll".format(src, opt)
            if src.endswith(".cl"):
                gen_spir(src, ir, opt=opt)
            else:
                run([
                    "clang", "-S", "-emit-llvm",
                    "--target=spir-unknown-unknown",
                    "-O{}".format(opt),
                    src, "-o", ir,
                ], check=True)
            irs.append(ir)
        
        lir = self.link(irs)
        
        dst = lir.rsplit(".", 1)[0] + ".cl"
        gen_oclc(lir, dst)
        return dst


    def makeref(self):
        return (self.a.copy(), self.a**2)

    def run(self, src, **kws):
        n = 64
        a = self.a.copy()
        b = np.zeros_like(a)
        run_kernel(self.ctx, src, (self.n,), *[Mem(x) for x in [a, b]])
        return (a, b)
