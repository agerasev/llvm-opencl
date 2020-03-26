#!/usr/bin/env python3

import os
import argparse
import atexit

from test import run


parser = argparse.ArgumentParser(
    description="Run LLVM-OpenCL translator tests."
)
parser.add_argument(
    "pattern", metavar="TEST", nargs="*", type=str,
    help="List of test patterns to run. If empty - run all."
)
parser.add_argument(
    "-p", "--platform", metavar="INDEX", type=int, default=-1,
    help="Index of OpenCL platform to run tests"
)
parser.add_argument(
    "-o", "--opt", metavar="LEVEL", nargs='+', type=int, default=[3],
    help="Clang optimization level while generating IR. `[3]` by default."
)
parser.add_argument(
    "-r", "--recurse", metavar="DEPTH", type=int, default=1,
    help="Recursively translate generated code."
)
parser.add_argument(
    "-e", "--exit-on-failure", action="store_true",
    help="Print information and exit on first occured test failure."
)
parser.add_argument(
    "-c", "--clean", action="store_true",
    help="Remove build and translation files."
)
args = parser.parse_args()

if args.recurse > 1 and len(args.opt) > 1:
    raise Exception("Cannot recurse with more than one optimization levels")

args.pattern = [p.split(".") for p in args.pattern]


# Enter testing
run(args)
