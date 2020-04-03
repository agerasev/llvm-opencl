import os

import numpy as np

from test.translate import translate


class Tester:
    def __init__(self, ctx, loc, src="source.cl"):
        self.ctx = ctx
        self.loc = loc
        if isinstance(src, str):
            self.src = os.path.join(loc, src)
        else:
            self.src = [os.path.join(loc, s) for s in src]
        self.ref = None

    def run(self, src, **kws):
        raise NotImplementedError()

    def makeref(self):
        return self.run(self.src)

    def translate(self, src, **kws):
        opt = kws["opt"]
        fe = {"opt": opt, "debug": kws.get("debug", False)}
        if "std" in kws:
            fe["std"] = kws["std"]
        return translate(src, suffix="o{}".format(opt), fe=fe)

    def check(self, res, **kws):
        assert len(self.ref) == len(res)
        for i, (f, s) in enumerate(zip(self.ref, res)):
            try:
                assert np.allclose(f, s)
            except AssertionError as e:
                raise AssertionError(
                    "\n".join([
                        "Mismatch in buffer {}".format(i),
                        str(f), "!=", str(s),
                    ])
                ) from e

    def test(self, src, **kws):
        try:
            dst = self.translate(src, **kws)
        except Exception as e:
            raise Exception(src) from e

        try:
            res = self.run(dst, **kws)
        except Exception as e:
            raise Exception(dst) from e

        try:
            self.check(res, **kws)
        except AssertionError as e:
            raise AssertionError(dst) from e
        
        return dst

    def test_all(self, args):
        self.ref = self.makeref()
        src = self.src
        for i in range(args.recurse):
            dst = None
            for opt in args.opt:
                dst = self.test(src, opt=opt)
            src = dst
