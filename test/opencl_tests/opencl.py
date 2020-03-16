#!/usr/bin/env python3

import numpy as np
import pyopencl as cl


class Mem:
    def __init__(self, content):
        self.content = content

def run_kernel(ctx, src_file, shape, *args, name="kernel_main"):
    queue = cl.CommandQueue(ctx)

    mf = cl.mem_flags
    
    kargs = []
    for arg in args:
        karg = arg
        if isinstance(arg, Mem):
            karg = cl.Buffer(ctx, mf.READ_WRITE | mf.COPY_HOST_PTR, hostbuf=arg.content)
        kargs.append(karg)

    with open(src_file, "r") as f:
        src = f.read()
    prg = cl.Program(ctx, src).build()
    getattr(prg, name)(queue, shape, None, *kargs)

    for arg, karg in zip(args, kargs):
        if isinstance(arg, Mem):
            cl.enqueue_copy(queue, arg.content, karg)

    queue.flush()
    queue.finish()
