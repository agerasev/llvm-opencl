import os
import shutil
import importlib

import numpy as np

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
                Cleaner(d).walk()

        if not os.listdir(self.loc):
            shutil.rmtree(self.loc)
            

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
        module = importlib.import_module(self.modname)
        
        try:
            if module.skip and not self.patterns:
                self.report.skip(self.modname)
                return
        except AttributeError:
            pass

        children = self.fork(dirs)
        if len(children) > 0:
            for child in children:
                child.walk()
            return
        
        try:
            Tester = module.Tester
        except AttributeError:
            self.report.warn(self.modname, "no `Tester` class")
            return
        
        try:
            Tester(self.ctx, self.loc).test_all(self.args)
        except Warning as w:
            self.report.warn(self.modname, str(w))
        except Exception as e:
            self.report.fail(self.modname, e)
        else:
            self.report.ok(self.modname)

def test(ctx, report, patterns, args):
    Runner(os.path.split(__file__)[0], ctx, __name__, report, patterns, args).walk()
