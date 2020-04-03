#!/usr/bin/env python3

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel
from test.cases.tester import Tester as BaseTester


class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src="source.cl")

    def run(self, src, **kws):
        n = 256
        a = np.pi*((np.arange(n, dtype=cltypes.float) % 13) - 13/2)
        b = np.pi*((np.arange(n, dtype=cltypes.float) % 17) - 17/2)
        c = (np.arange(n, dtype=cltypes.int) % 5) - 5//2
        buf = [a, b, c]
        
        for i in range(13):
            buf.append(np.zeros(n, dtype=cltypes.float))
        
        for i in range(4):
            buf.append(np.e*((np.arange(n, dtype=cltypes.float) % 7) - 7/2))

        run_kernel(self.ctx, src, (n,), *[Mem(x) for x in buf])
        return buf
