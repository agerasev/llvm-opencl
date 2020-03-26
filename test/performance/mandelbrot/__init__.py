#!/usr/bin/env python3

import os

import numpy as np
import pyopencl as cl
from pyopencl import cltypes

from test.opencl import Mem, run_kernel


def run(ctx, src):
    w, h = 1024, 512
    depth = 1024
    img = np.zeros((h, w), dtype=cltypes.float)

    time = run_kernel(
        ctx, src, (w, h),
        Mem(img), *[cltypes.uint(k) for k in (w, h, depth)],
    )
    """
    for row in img[::16,::16]:
        for z in row:
            print("@" if z > 0.5 else ".", end="")
        print()
    """
    print("\t{:.3f} sec: {}".format(time, os.path.split(src)[1]))

    return (img,)


def compare(ref, res):
    assert len(ref) == len(res)
    for f, s in zip(ref, res):
        diff = np.abs(f - s)
        mask = diff > 1e-5
        # Significant difference occured in less than 1/1000 points
        assert np.mean(mask) < 1e-3
        diff[mask] = 0.0
        assert np.allclose(diff, 0.0)
