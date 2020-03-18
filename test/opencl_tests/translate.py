#!/usr/bin/env python3

from subprocess import run


def gen_spir(src, ll, O=3):
    run([
        "clang-8", "-x", "cl", "-S", "-emit-llvm",
        "--target=spir-unknown-unknown",
        "-std=cl1.2",
        "-Xclang", "-finclude-default-header",
        "-O{}".format(O),
        src, "-o", ll,
    ], check=True)

def gen_ocls(ll, cbe_cl):
    run(["llvm-cbe", ll, "-o", cbe_cl], check=True)

def translate(src, **opts):
    ll = "{}.ll".format(src)
    cbe_cl = "{}.cbe.cl".format(src)
    gen_spir(src, ll, **opts)
    gen_ocls(ll, cbe_cl)
