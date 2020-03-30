#!/usr/bin/env python3

from subprocess import run, SubprocessError

class FrontendError(Exception):
    def __init__(self, message):
        super().__init__(message)

class BackendError(Exception):
    def __init__(self, message):
        super().__init__(message)

class LinkError(Exception):
    def __init__(self, message):
        super().__init__(message)

def gen_spir(src, ir, opt=3, std="cl1.2"):
    try:
        run([
            "clang-9", "-x", "cl", "-S", "-emit-llvm",
            "--target=spir-unknown-unknown",
            "-std={}".format(std),
            "-Xclang", "-finclude-default-header",
            "-O{}".format(opt),
            src, "-o", ir,
        ], check=True)
    except SubprocessError as e:
        raise FrontendError(src) from e

def gen_oclc(ir, dst):
    try:
        run(["llvm-opencl", ir, "-o", dst], check=True)
    except SubprocessError as e:
        raise BackendError(ir) from e

def translate(src, dst=None, suffix="", fe={}, be={}):
    ir = "{}.gen.ll".format(src + suffix)
    if not dst:
        dst = "{}.gen.cl".format(src + suffix)
    gen_spir(src, ir, **fe)
    gen_oclc(ir, dst, **be)
    return dst

def link(srcs, dst, suffix=""):
    try:
        run(["llvm-link", "-S", *srcs, "-o", dst], check=True)
    except SubprocessError as e:
        raise LinkError(str(srcs)) from e
    return dst
