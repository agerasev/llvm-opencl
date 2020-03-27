import os

import pyopencl as cl

from test.misc import remove_content
from test.translate import translate

names = [
    "mandelbrot"
]
dirs = [os.path.join(os.path.split(__file__)[0], n) for n in names]

def clean():
    for d in dirs:
        remove_content(
            d,
            names=["__pycache__"],
            extensions=[".gen.ll", ".gen.cl", ".png"],
        )

def try_build(ctx, src):
    with open(src, "r") as f:
        cl.Program(ctx, f.read()).build()

def test(ctx, report, pattern, args):
    for n, d in zip(names, dirs):
        try:
            for item in os.listdir(d):
                if item.endswith(".cl") and not item.endswith(".gen.cl"):
                    src = os.path.join(d, item)
                    try_build(ctx, src)
                    for opt in args.opt:
                        try_build(ctx, translate(
                            src, fe={"opt": opt},
                            suffix=".o{}".format(opt)
                        ))
        except Exception as e:
            report.fail("{}.{}".format(__name__, n), e)
        else:
            report.ok("{}.{}".format(__name__, n))
