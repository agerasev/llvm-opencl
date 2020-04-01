#!/usr/bin/env python3

import numpy as np
import pyopencl as cl

from test.opencl import run_kernel
from test.cases.tester import Tester as BaseTester


class Tester(BaseTester):
    def __init__(self, *args):
        super().__init__(*args, src="source.cl")

    def run(self, src, **kws):
        run_kernel(self.ctx, src, (1,))
        return ()
