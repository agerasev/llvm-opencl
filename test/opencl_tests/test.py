#!/usr/bin/env python3

import os, sys
import importlib
import numpy as np
import pyopencl as cl

from translate import translate

if __name__ == "__main__":
    O = os.environ.get("OPT", 3)
    npass = int(os.environ.get("PASS", 1))

    ctx = cl.create_some_context()

    for d in sorted([f for f in os.listdir(".") if os.path.isdir(f) and f.startswith("test_")]):
        if len(sys.argv) > 1:
            if not any([a in d for a in sys.argv]):
                continue
        try:
            module = importlib.import_module(d)
            ref = None
            src = os.path.join(d, "source.cl")
            for i in range(npass + 1):
                if i > 0:
                    translate(src, O=O)
                    src += ".cbe.cl"
                res = module.run(ctx, src)
                if ref:
                    for f, s in zip(ref, res):
                        assert np.allclose(f, s)
                ref = res

        except Exception as e:
            print("[{}] failed".format(d))
            raise
        else:
            print("[{}] passed".format(d))
