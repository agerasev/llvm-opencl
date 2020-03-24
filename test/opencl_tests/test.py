#!/usr/bin/env python3

import os, sys
import shutil
import importlib
import argparse
from subprocess import SubprocessError

import numpy as np
import pyopencl as cl

from translate import translate


def split_path(path):
    head, tail = os.path.split(path)
    return (split_path(head) if head else []) + [tail]

def search_tests(basedir, ignore=["__pycache__"]):
    walker = sorted(list(os.walk(".")), key=lambda t: t[0])

    for dirpath, dirnames, filenames in walker:
        if dirpath == ".":
            continue

        dirlist = split_path(dirpath)[1:]
        if any([d in ignore for d in dirlist]):
            continue

        dirnames = [d for d in dirnames if d not in ignore]

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
        "--opt", metavar="LEVEL", nargs='+', type=int, default=[3],
        help="Clang optimization level while generating IR. `[3]` by default."
    )
    parser.add_argument(
        "--recurse", metavar="DEPTH", type=int, default=1,
        help="Recursively translate generated code."
    )
    parser.add_argument(
        "--exit-on-failure", action="store_true",
        help="Print information and exit on first occured test failure."
    )
    parser.add_argument(
        "--clean", action="store_true",
        help="Remove build and translation files."
    )
    args = parser.parse_args()

    if args.clean:
        for dirlist, dirnames, filenames in search_tests(".", ignore=[]):
            for fn in filenames:
                if any([fn.endswith(ext) for ext in (".ll", ".cbe.cl", ".cbe.c", ".o0", ".o3")]):
                    os.remove(os.path.join(*dirlist, fn))
            for dn in dirnames:
                if dn == "__pycache__":
                    shutil.rmtree(os.path.join(*dirlist, dn))
        exit()

    if args.recurse > 1 and len(args.opt) > 1:
        raise Exception("Cannot recurse with more than one optimization levels")

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


    passed, failed, warnings = 0, 0, 0
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
        
        module = importlib.import_module(modpath)
        if not hasattr(module, "run"):
            if len(dirnames) == 0:
                print("[warn] no tests ran for {}".format(modpath))
                warnings += 1
            continue

        src = os.path.join(*dirlist, "source.cl")
        try:
            try:
                ref = module.run(ctx, src)
            except Exception as e:
                raise Exception(src) from e

            for i in range(args.recurse):
                dst = None
                for opt in args.opt:
                    
                    dst = translate(
                        src, opt=opt,
                        suffix=".o{}".format(opt)
                    )

                    try:
                        res = module.run(ctx, dst)
                    except Exception as e:
                        raise Exception(dst) from e

                    try:
                        for i, (f, s) in enumerate(zip(ref, res)):
                            assert np.allclose(f, s)
                    except AssertionError as e:
                        print(f)
                        print("!=")
                        print(s)
                        print("In buffer {}".format(i))
                        raise AssertionError(dst) from e

                src = dst
        except Exception as e:
            print("[fail] {}".format(modpath))
            failed += 1
            if args.exit_on_failure:
                raise
        else:
            print("[ok] {}".format(modpath))
            passed += 1
        
    print("done, {} passed, {} failed".format(passed, failed), end="")
    print(", {} warnings".format(warnings) if warnings > 0 else "")

    if failed > 0:
        exit(1)
