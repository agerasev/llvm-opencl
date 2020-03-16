#!/usr/bin/env python3

import os
import importlib
from subprocess import run
import numpy as np
import pyopencl as cl


if __name__ == "__main__":
    ctx = cl.create_some_context()

    for d in sorted([f for f in os.listdir(".") if os.path.isdir(f) and f.startswith("test_")]):
        module = importlib.import_module(d)
        src = os.path.join(d, "source.cl")
        ref = module.run(ctx, src)
        run(["./translate.sh", src], check=True)
        res = module.run(ctx, os.path.join(d, "source.cl.cbe.c"))
        assert np.allclose(ref, res)
