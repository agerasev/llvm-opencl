#!/usr/bin/env bash

clang-8 -x cl -S -emit-llvm \
    --target=spir-unknown-unknown \
    -std=cl1.2 \
    -Xclang \
    -finclude-default-header \
    -O3 \
    $1 -o $1.ll
