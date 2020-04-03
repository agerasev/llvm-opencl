#!/usr/bin/env python3

from os.path import split, join
from subprocess import run, SubprocessError


bits = 32
#bits = 64

if bits == 64:
    target = "spir64-unknown-unknown"
    wasm_target = "wasm64-unknown-unknown"
    data_layout = "e-p:32:32-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024"
else:
    target = "spir-unknown-unknown"
    wasm_target = "wasm32-unknown-unknown"
    data_layout = "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024"


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

def frontend(src, ir, opt=3, ty=None, std=None, debug=False):
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
                "--target={}".format(target),
                "-std={}".format(std),
                "-Xclang", "-finclude-default-header",
                "-O{}".format(opt),
                *(["-g"] if debug else []),
                src, "-o", ir,
            ], check=True)

        elif (
            (ty and (ty == "c" or ty == "cpp")) or
            (not ty and (src.endswith(".c") or src.endswith(".cpp")))
        ):
            # Plain C/C++ source file
            run([
                "clang", "-S", "-emit-llvm",
                "--target={}".format(target),
                *(["-std={}".format(std)] if std else []),
                "-O{}".format(opt),
                *(["-g"] if debug else []),
                src, "-o", ir,
            ], check=True)

        elif (ty and ty == "rs") or (not ty and src.endswith(".rs")):
            # Rust library, you need to have `rustc` installed
            rsir = "{}.o{}.rs.gen.ll".format(src, opt)
            run([
                "rustc", "--emit=llvm-ir",
                # Use WASM target because rustc cannot compile to SPIR yet
                "--target={}".format(wasm_target),
                "--crate-type=lib",
                "-C", "opt-level={}".format(opt),
                src, "-o", rsir,
            ], check=True)

            # Translate `wasm32` to `spir` target
            run([
                "opt", "-S",
                "--mtriple={}".format(target),
                "--data-layout={}".format(data_layout),
                "-O{}".format(opt),
                *(["-g"] if debug else []),
                rsir, "-o", ir,
            ], check=True)

        elif (ty and ty == "spir") or (not ty and src.endswith(".ll")):
            # Already LLVM IR, apply optimization
            run([
                "opt", "-S",
                "-O{}".format(opt),
                src, "-o", ir,
            ], check=True)
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
