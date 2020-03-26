import os
import shutil
import importlib
from subprocess import SubprocessError

import numpy as np

from test.translate import translate
from test.walker import Walker
from test.misc import distribute_patterns


class Cleaner(Walker):
    def __init__(self, loc):
        super().__init__(loc)
    
    def process(self, files, dirs):
        for f in files:
            if any([f.endswith(ext) for ext in [".gen.ll", ".gen.cl"]]):
                os.remove(f)

        children = []
        for d in dirs:
            if os.path.split(d)[1] in ["__pycache__"]:
                shutil.rmtree(d)
            else:
                children.append(Cleaner(d))

        return children

def clean():
    Cleaner(os.path.split(__file__)[0]).walk()


class Runner(Walker):
    def __init__(self, loc, ctx, modname, report, patterns, args):
        super().__init__(loc)
        self.ctx = ctx
        self.modname = modname
        self.report = report
        self.patterns = patterns
        self.args = args

    def fork(self, dirs):
        dirs = [d for d in dirs if os.path.split(d)[1] != "__pycache__"]
        dd = {os.path.split(d)[1]: d for d in dirs}
        children = []
        
        for d, p in distribute_patterns(list(dd.keys()), self.patterns):
            children.append(Runner(
                dd[d], self.ctx,
                self.modname + "." + d,
                self.report, p, self.args,
            ))
        return children
    
    def process(self, files, dirs):
        children = self.fork(dirs)
        if len(children) > 0:
            return children
        
        module = importlib.import_module(self.modname)
        if not hasattr(module, "run"):
            self.report.warn(self.modname, "no `run` function")
            return
        
        src = os.path.join(self.loc, "source.cl")
        try:
            try:
                ref = module.run(self.ctx, src)
            except Exception as e:
                raise Exception(src) from e

            for i in range(self.args.recurse):
                dst = None
                for opt in self.args.opt:
                    
                    dst = translate(
                        src, opt=opt,
                        suffix=".o{}".format(opt)
                    )

                    try:
                        res = module.run(self.ctx, dst)
                    except Exception as e:
                        raise Exception(dst) from e

                    try:
                        if hasattr(module, "compare"):
                            module.compare(ref, res)
                        else:
                            assert len(ref) == len(res)
                            for i, (f, s) in enumerate(zip(ref, res)):
                                try:
                                    assert np.allclose(f, s)
                                except AssertionError as e:
                                    raise AssertionError(
                                        "Mismatch in buffer {}".format(i)
                                    ) from e
                    except AssertionError as e:
                        raise AssertionError(dst) from e

                src = dst
        except Exception as e:
            self.report.fail(self.modname, e)
        else:
            self.report.ok(self.modname)


def test(ctx, report, patterns, args):
    Runner(os.path.split(__file__)[0], ctx, __name__, report, patterns, args).walk()
