#!/usr/bin/env python3

from time import time, sleep
import numpy as np
import pyopencl as cl


class Mem:
    def __init__(self, content):
        self.content = content

def run_kernel(ctx, src_file, shape, *args, name="kernel_main", src=None):
    queue = cl.CommandQueue(ctx)

    mf = cl.mem_flags
    
    kargs = []
    for arg in args:
        karg = arg
        if isinstance(arg, Mem):
            karg = cl.Buffer(ctx, mf.READ_WRITE | mf.COPY_HOST_PTR, hostbuf=arg.content)
        kargs.append(karg)

    if not src:
        src = ""
        if isinstance(src_file, str):
            src_file = [src_file]
        for sf in src_file:
            with open(sf, "r") as f:
                src += f.read() + "\n"
    
    prg = cl.Program(ctx, src).build()
    queue.flush()
    queue.finish()

    begin = time()
    getattr(prg, name)(queue, shape, None, *kargs)
    queue.flush()
    queue.finish()
    end = time()

    for arg, karg in zip(args, kargs):
        if isinstance(arg, Mem):
            cl.enqueue_copy(queue, arg.content, karg)

    queue.flush()
    queue.finish()

    return end - begin
