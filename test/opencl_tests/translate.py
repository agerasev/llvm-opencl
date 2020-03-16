#!/usr/bin/env python3

from subprocess import run


def gen_spir(src, O=3):
    ll = "{}.ll".format(src)
    run([
        "clang-8", "-x", "cl", "-S", "-emit-llvm",
        "--target=spir-unknown-unknown",
        "-std=cl1.2",
        "-Xclang", "-finclude-default-header",
        "-O{}".format(O),
        src, "-o", ll,
    ], check=True)
    return ll

def gen_ocls(ll):
    run(["llvm-cbe", ll], check=True)

def translate(src, **opts):
    ll = gen_spir(src, **opts)
    gen_ocls(ll)
