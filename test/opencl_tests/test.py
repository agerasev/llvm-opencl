#!/usr/bin/env python3

import os, sys
import shutil
import importlib
import argparse
import numpy as np
import pyopencl as cl

from translate import translate

def split_path(path):
    head, tail = os.path.split(path)
    return (split_path(head) if head else []) + [tail]

def search_tests(basedir):
    walker = sorted(list(os.walk(".")), key=lambda t: t[0])

    for dirpath, dirnames, filenames in walker:
        if dirpath == ".":
            continue

        dirlist = split_path(dirpath)[1:]

        if all([d.startswith("test_") for d in dirlist]):
            yield (dirlist, dirnames, filenames)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Run LLVM-OpenCL translator tests."
    )
    parser.add_argument(
        "tests", metavar="TEST", nargs="*", type=str,
        help="List of test patterns to run. If empty - run all."
    )
    parser.add_argument(
        "--platform", metavar="INDEX", type=int, default=-1,
        help="Index of OpenCL platform to run tests"
    )
    parser.add_argument(
        "--opt", metavar="LEVEL", type=int, default=3,
        help="Clang optimization level while generating IR. '3' by default."
    )
    parser.add_argument(
        "--recurse", metavar="DEPTH", type=int, default=1,
        help="Recursively translate generated code."
    )
    parser.add_argument(
        "--clean", action="store_true",
        help="Remove build and translation files."
    )
    args = parser.parse_args()
    

    if args.clean:
        for dirlist, dirnames, filenames in search_tests("."):
            for fn in filenames:
                if any([fn.endswith(ext) for ext in (".ll", ".cbe.cl", ".cbe.c")]):
                    os.remove(os.path.join(*dirlist, fn))
            for dn in dirnames:
                if dn == "__pycache__":
                    shutil.rmtree(os.path.join(*dirlist, dn))
        exit()


    args.tests = [p.split(".") for p in args.tests]
    os.environ["PYOPENCL_COMPILER_OUTPUT"] = "1"

    if args.platform < 0:
        ctx = cl.create_some_context()
    else:
        platform = cl.get_platforms()[args.platform]
        print("Using platform: {}".format(platform.get_info(cl.platform_info.NAME)))
        device = platform.get_devices()[0]
        print("Using device: {}".format(device.get_info(cl.device_info.NAME)))
        ctx = cl.Context(devices=[device])


    for dirlist, dirnames, _ in search_tests("."):
        if args.tests:
            skip = False
            for plist in args.tests:
                for d, p in zip(dirlist, plist):
                    if p not in d:
                        skip = True
                        break
                if skip:
                    break
            if skip:
                continue

        modpath = ".".join(dirlist)
        try:
            module = importlib.import_module(modpath)
            if not hasattr(module, "run"):
                assert any([d.startswith("test_") for d in dirnames])
                continue
            ref = None
            src = os.path.join(*dirlist, "source.cl")
            for i in range(args.recurse + 1):
                if i > 0:
                    translate(src, O=args.opt)
                    src += ".cbe.cl"
                res = module.run(ctx, src)
                if ref:
                    for f, s in zip(ref, res):
                        assert np.allclose(f, s)
                ref = res

        except Exception as e:
            print("[fail] {}".format(modpath))
            print("src: {}".format(src))
            raise
        else:
            print("[ok] {}".format(modpath))
