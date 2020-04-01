#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.cases.tester import Tester as BaseTester


class OperationTester(BaseTester):
    def __init__(self, UT, ST, B, N, *args):
        super().__init__(*args, src="source.ll")

        self.UT = UT
        self.ST = ST
        self.B = B
        self.M = (1 << B) - 1
        self.N = N

        n = 64
        a = (self.B*np.arange(self.N*n, dtype=self.UT)) & self.M
        b = (np.arange(self.N*n, dtype=self.UT)>>1) & self.M
        c = (np.arange(self.N*n, dtype=self.UT) % self.M) + 1 # non-zero
        d = np.arange(self.N*n, dtype=self.UT) % self.B # 0 <= x < bit_width

        self.n = n
        self.ibuf = [a, b, c, d]

    # Reinterpret
    def ru(self, x):
        return x.astype(self.UT)
    def rs(self, x):
        return x.astype(self.ST)

    # Convert
    def cu(self, x):
        return self.ru(x) & self.M
    def cs(self, x):
        return self.rs(x | ((~self.M) * ((x >> (self.B - 1)) & 1)))


    def makeref(self):
        a, b, c, d = self.ibuf

        ibuf = [a, b, c, d]
        
        obuf = [
            a, # trunc
            a, # zext
            self.ru(self.cs(a)), # sext

            (a + b) & self.M, # add
            (a - b) & self.M, # sub
            (a * b) & self.M, # mul
            self.cu(a / c) & self.M, # udiv
            self.cu(self.cs(a) / self.cs(c)) & self.M, # sdiv
            self.cu(a % c) & self.M, # urem
            self.cu(np.fmod(self.cs(a), self.cs(c))) & self.M, # srem

            (a << d) & self.M, # shl
            a >> d, # lshr
            self.cu(self.cs(a) >> d), # ashr
            a & b, # and
            a | b, # or
            a ^ b, # xor

            self.cu(a == b), # icmp eq
            self.cu(a != b), # icmp ne
            self.cu(a > b), # icmp ugt
            self.cu(a >= b), # icmp uge
            self.cu(a < b), # icmp ult
            self.cu(a <= b), # icmp ule
            self.cu(self.cs(a) > self.cs(b)), # icmp sgt
            self.cu(self.cs(a) >= self.cs(b)), # icmp sge
            self.cu(self.cs(a) < self.cs(b)), # icmp slt
            self.cu(self.cs(a) <= self.cs(b)), # icmp sle

            np.select([(a & 1) > 0], [b], [c]), # select
        ]

        return ibuf + obuf

    def run(self, src, **kws):
        buf = [x for x in self.ibuf]
        for i in range(27):
            buf.append(np.zeros(self.N*self.n, dtype=self.UT))

        run_kernel(self.ctx, src, (self.n,), *[Mem(x) for x in buf])

        return buf
