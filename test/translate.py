#!/usr/bin/env python3

from subprocess import run, SubprocessError

class FrontendError(Exception):
    def __init__(self, message):
        super().__init__(message)

class BackendError(Exception):
    def __init__(self, message):
        super().__init__(message)

def gen_spir(src, ir, opt=3):
    try:
        run([
            "clang-8", "-x", "cl", "-S", "-emit-llvm",
            "--target=spir-unknown-unknown",
            "-std=cl1.2",
            "-Xclang", "-finclude-default-header",
            "-O{}".format(opt),
            src, "-o", ir,
        ], check=True)
    except SubprocessError as e:
        raise FrontendError(src) from e

def gen_ocls(ir, dst):
    try:
        run(["llvm-opencl", ir, "-o", dst], check=True)
    except SubprocessError as e:
        raise BackendError(ir) from e

def translate(src, dst=None, opt=3, suffix=""):
    ir = "{}.ll".format(src + suffix)
    if not dst:
        dst = "{}.cbe.cl".format(src + suffix)
    gen_spir(src, ir, opt)
    gen_ocls(ir, dst)
    return dst
