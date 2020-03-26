import os
from os.path import join, isfile, isdir


class Walker:
    def __init__(self, loc):
        self.loc = loc

    def process(self, files, dirs):
        raise NotImplementedError

    def walk(self):
        items = [join(self.loc, i) for i in sorted(os.listdir(self.loc))]
        files = [f for f in items if isfile(f)]
        dirs = [d for d in items if isdir(d)]

        children = self.process(files, dirs)
        if children:
            for child in children:
                child.walk()
