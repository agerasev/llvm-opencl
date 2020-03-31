#!/usr/bin/env python3

from os.path import split, join
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

def check_rustc():
    try:
        run(["rustc", "-V"], check=True)
    except:
        return False
    else:
        return True

def frontend(src, ir, opt=3, ty=None, std=None):
    try:
        if (ty and ty.startwith("cl")) or (not ty and src.endswith(".cl")):
            # OpenCL source file
            if (ty and ty == "clcpp") or (not ty and src.endswith(".cpp.cl")):
                # OpenCL C++
                if not std:
                    std = "clc++"
            if not std:
                std = "cl1.2"
            run([
                "clang", "-x", "cl", "-S", "-emit-llvm",
                "--target=spir-unknown-unknown",
                "-std={}".format(std),
                "-Xclang", "-finclude-default-header",
                "-O{}".format(opt),
                src, "-o", ir,
            ], check=True)

        elif (
            (ty and (ty == "c" or ty == "cpp")) or
            (not ty and (src.endswith(".c") or src.endswith(".cpp")))
        ):
            # Plain C/C++ source file
            args = [
                "clang", "-S", "-emit-llvm",
                "--target=spir-unknown-unknown",
            ]
            if std:
                args.append("-std={}".format(std))
            args.append("-O{}".format(opt))
            args.extend([src, "-o", ir])
            run(args, check=True)

        elif (ty and ty == "rs") or (not ty and src.endswith(".rs")):
            # Rust library, you need to have `rustc` installed
            args = [
                "rustc", "--emit=llvm-ir",
                # Use `wasm32` because rustc cannot compile to `spir`
                "--target=wasm32-unknown-unknown",
                "--crate-type=lib",
            ]
            if opt >= 2:
                args.append("-O")
            args.extend([src, "-o", ir])
            run(args, check=True)
        else:
            if ty:
                raise Exception("Unknown source type: {}".format(ty))
            else:
                raise Exception("Unknown source extension: {}".foramt(src))

    except SubprocessError as e:
        raise FrontendError(src) from e

def backend(ir, dst):
    try:
        run(["llvm-opencl", ir, "-o", dst], check=True)
    except SubprocessError as e:
        raise BackendError(ir) from e

def link(irs, dst):
    try:
        run(["llvm-link", "-S", *irs, "-o", dst], check=True)
    except SubprocessError as e:
        raise LinkError(str(irs)) from e
    return dst

def translate(src, dst=None, suffix="", fe={}, lnk={}, be={}):
    if isinstance(src, str):
        dst = "{}.{}.gen.cl".format(src, suffix)
        src = [src]
    else:
        d, n = split(src[0])
        e = n.split(".", 1)[1]
        gen = join(d, "all.{}.{}.gen".format(e, suffix))
        lir = "{}.ll".format(gen)
        dst = "{}.cl".format(gen)

    irs = []
    for s in src:
        ir = "{}.{}.gen.ll".format(s, suffix)
        frontend(s, ir, **fe)
        irs.append(ir)

    if len(irs) > 1:
        link(irs, lir, **lnk)
    else:
        lir = irs[0]

    backend(lir, dst, **be)

    return dst
