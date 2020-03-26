import atexit

import colorama
import pyopencl as cl

from test.misc import distribute_patterns

from test import cases, examples, performance
modules = [cases, performance, examples]

CG = colorama.Fore.GREEN
CY = colorama.Fore.YELLOW
CR = colorama.Fore.RED
C_ = colorama.Style.RESET_ALL

class Report:
    def __init__(self, eof=False):
        self.passed = 0
        self.failed = 0
        self.warning = 0
        self.eof = eof

    def ok(self, name):
        self.passed += 1
        print("[ {}ok{} ] {}".format(CG, C_, name.split(".", 1)[1]))

    def fail(self, name, exc):
        self.failed += 1
        print("[ {}fail{} ] {}: {}".format(CR, C_, name.split(".", 1)[1], str(exc)))
        if self.eof:
            raise exc
    
    def warn(self, name, msg):
        self.warning += 1
        print(("[ {}warn{} ] {}: {}").format(CY, C_, name.split(".", 1)[1], msg))

    def print_result(self):
        print(CG if self.failed == 0 else CR, end="")
        print("done, {} passed, {} failed".format(self.passed, self.failed), end="")
        print("{}, {} warnings".format(CY, self.warning) if self.warning > 0 else "")
        print(C_, end="")


def run(args):
    colorama.init()
    atexit.register(lambda: colorama.deinit())

    if args.clean:
        clean()
        exit()
    
    test(args)

def clean():
    for m in modules:
        m.clean()

def test(args):
    if args.platform < 0:
        ctx = cl.create_some_context()
    else:
        platform = cl.get_platforms()[args.platform]
        print("Using platform: {}".format(platform.get_info(cl.platform_info.NAME)))
        device = platform.get_devices()[0]
        print("Using device: {}".format(device.get_info(cl.device_info.NAME)))
        ctx = cl.Context(devices=[device])

    report = Report(eof=args.exit_on_failure)
    patterns = args.pattern
    mods = {m.__name__.split(".")[-1]: m for m in modules}
    for m, p in distribute_patterns(list(mods.keys()), patterns):
        mods[m].test(ctx, report, p, args)

    report.print_result()
    if report.failed > 1:
        exit()
