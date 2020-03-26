#!/usr/bin/env python3

import sys
import numpy as np
import pyopencl as cl
from pyopencl import cltypes
from PIL import Image

if __name__ == "__main__":
    # Initialize OpenCL
    ctx = cl.create_some_context()
    queue = cl.CommandQueue(ctx)
    mf = cl.mem_flags
    
    # Read command line argument
    if len(sys.argv) > 1:
        src_path = sys.argv[1]
    else:
        src_path = "kernel.cl"

    # Render parameters
    width, height = 1024, 768
    depth = 256
    ssf = 2 # supersampling factor

    # Host and device buffers to store rendered image
    image = np.zeros((height, width, 3), dtype=cltypes.float)
    image_buf = cl.Buffer(ctx, mf.WRITE_ONLY | mf.COPY_HOST_PTR, hostbuf=image)

    # Buffer to store transformation
    map_buf = cl.Buffer(ctx, mf.READ_ONLY | mf.COPY_HOST_PTR, hostbuf=np.array([
        [2.0, 0.0], # zoom and rotation
        [0.7, 0.0], # shift
    ], dtype=cltypes.float))

    # Coloring of the Mandelbrot exterior
    colors = np.array([
        [0.0, 0.0, 1.0], # blue
        [1.0, 1.0, 0.0], # yellow
    ], dtype=cltypes.float)
    colors_buf = cl.Buffer(ctx, mf.READ_ONLY | mf.COPY_HOST_PTR, hostbuf=colors)
    color_width = 8.0

    # Read source file
    with open(src_path, "r") as f:
        src = f.read()
    # Build program
    prg = cl.Program(ctx, src).build()

    # Execute OpenCL kernel on the device
    prg.render(
        queue, (width, height), None,
        image_buf, map_buf, colors_buf,
        *[cltypes.uint(x) for x in [width, height, depth, ssf]],
        cltypes.uint(len(colors)), cltypes.float(color_width)
    )

    # Copy rendered image to host
    cl.enqueue_copy(queue, image, image_buf)

    # Flush queue
    queue.flush()
    queue.finish()

    # Draw low-resolution image in terminal
    for row in np.mean(image[::32,::16], axis=2):
        for x in row:
            print("@" if x > 1e-4 else ".", end="")
        print()

    # Save image to .png file
    im = Image.fromarray((255*image).astype(np.ubyte))
    im.save(src_path + ".png")
