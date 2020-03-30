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
        super().__init__(*args, src=("main.cl", "lib.rs"))
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
            elif src.endswith(".rs"):
                run([
                    "rustc", "--emit=llvm-ir",
                    "--target=wasm32-unknown-unknown",
                    "--crate-type=lib",
                    "-O",
                    src, "-o", ir,
                ], check=True)
            else:
                raise Exception("Unknown file extension: {}".format(src))
            irs.append(ir)
        
        lir = self.link(irs)
        print(lir)
        
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
