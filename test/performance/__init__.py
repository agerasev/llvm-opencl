import os

from test.misc import remove_content
from test.translate import translate

from . import mandelbrot

modules = [
    mandelbrot
]

def clean():
    for m in modules:
        bdir = os.path.split(m.__file__)[0]
        remove_content(
            bdir,
            names=["__pycache__"],
            extensions=[".gen.ll", ".gen.cl"],
        )


def test(ctx, report, pattern, args):
    for m in modules:
        bdir = os.path.split(m.__file__)[0]
        try:
            src = os.path.join(bdir, "source.cl")
            ref = m.run(ctx, src)
            for opt in args.opt:
                res = m.run(ctx, translate(
                    src, fe={"opt": opt},
                    suffix="o{}".format(opt)
                ))
                m.compare(ref, res)
        except Exception as e:
            report.fail(m.__name__, e)
        else:
            report.ok(m.__name__)
