#!/usr/bin/env python3

from pyopencl import cltypes

from ... import OperationTester


class Tester(OperationTester):
    def __init__(self, *args):
        super().__init__(
            cltypes.uchar, # UT
            cltypes.char, # ST
            1, # B
            1, # N
            *args
        )
