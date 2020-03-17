#!/usr/bin/env python3

import os, sys
import importlib
import numpy as np
import pyopencl as cl

from translate import translate

if __name__ == "__main__":
    O = os.environ.get("OPT", 3)
    ctx = cl.create_some_context()

    for d in sorted([f for f in os.listdir(".") if os.path.isdir(f) and f.startswith("test_")]):
        try:
            module = importlib.import_module(d)
            src = os.path.join(d, "source.cl")
            ref = module.run(ctx, src)
            translate(src, O=O)
            res = module.run(ctx, os.path.join(d, "source.cl.cbe.c"))
            for f, s in zip(ref, res):
                assert np.allclose(f, s)
        except Exception as e:
            print("[{}] failed".format(d))
            raise
        else:
            print("[{}] passed".format(d))
