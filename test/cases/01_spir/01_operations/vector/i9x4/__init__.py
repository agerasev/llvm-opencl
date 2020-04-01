#!/usr/bin/env python3

from pyopencl import cltypes

from ... import OperationTester


class Tester(OperationTester):
    def __init__(self, *args):
        super().__init__(
            cltypes.ushort, # UT
            cltypes.short, # ST
            9, # B
            4, # N
            *args
        )
