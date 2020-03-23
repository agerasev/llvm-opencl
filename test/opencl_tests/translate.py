#!/usr/bin/env python3

from subprocess import run


def gen_spir(src, ir, opt=3):
    run([
        "clang-8", "-x", "cl", "-S", "-emit-llvm",
        "--target=spir-unknown-unknown",
        "-std=cl1.2",
        "-Xclang", "-finclude-default-header",
        "-O{}".format(opt),
        src, "-o", ir,
    ], check=True)

def gen_ocls(ir, dst):
    run(["llvm-cbe", ir, "-o", dst], check=True)

def translate(src, dst=None, opt=3):
    ir = "{}.o{}.ll".format(src, opt)
    if not dst:
        dst = "{}.o{}.cbe.cl".format(src, opt)
    gen_spir(src, ir, opt)
    gen_ocls(ir, dst)
    return dst
