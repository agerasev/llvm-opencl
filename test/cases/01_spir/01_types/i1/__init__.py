#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.cases.tester import Tester as BaseTester

UT = cltypes.uchar
ST = cltypes.char

B = 1
M = (1 << B) - 1

# Reinterpret
def ru(x):
    return x.astype(UT)
def rs(x):
    return x.astype(ST)

# Convert
def cu(x):
    return ru(x) & M
def cs(x):
    return rs(x | ((~M) * ((x >> (B - 1)) & 1)))


class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src="source.ll")

        n = 8
        a = np.arange(n, dtype=UT) & M
        b = (np.arange(n, dtype=UT)>>1) & M
        c = (np.arange(n, dtype=UT) % M) + 1 # non-zero
        d = np.arange(n, dtype=UT) % B # 0 <= x < bit_width

        self.n = n
        self.ibuf = [a, b, c, d]

    def makeref(self):
        a, b, c, d = self.ibuf

        ibuf = [a, b, c, d]
        
        obuf = [
            a, # trunc
            a, # zext
            ru(cs(a)), # sext

            (a + b) & M, # add
            (a - b) & M, # sub
            (a * b) & M, # mul
            cu(a / c) & M, # udiv
            cu(cs(a) / cs(c)) & M, # sdiv
            cu(a % c) & M, # urem
            cu(cs(a) % cs(c)) & M, # srem

            (a << d) & M, # shl
            a >> d, # lshr
            cu(cs(a) >> d), # ashr
            a & b, # and
            a | b, # or
            a ^ b, # xor

            cu(a == b), # icmp eq
            cu(a != b), # icmp ne
            cu(a > b), # icmp ugt
            cu(a >= b), # icmp uge
            cu(a < b), # icmp ult
            cu(a <= b), # icmp ule
            cu(cs(a) > cs(b)), # icmp sgt
            cu(cs(a) >= cs(b)), # icmp sge
            cu(cs(a) < cs(b)), # icmp slt
            cu(cs(a) <= cs(b)), # icmp sle

            np.select([(a & 1) > 0], [b], [c]), # select
        ]

        return ibuf + obuf

    def run(self, src, **kws):
        buf = [x for x in self.ibuf]
        for i in range(27):
            buf.append(np.zeros(self.n, dtype=UT))

        run_kernel(self.ctx, src, (self.n,), *[Mem(x) for x in buf])

        return buf
