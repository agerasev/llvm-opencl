#!/usr/bin/env python3

import numpy as np
import pyopencl as cl

from opencl import run_kernel


def run(ctx, src):
    run_kernel(ctx, src, (1,))
    return ()
