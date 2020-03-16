#!/usr/bin/env bash

./gen_spir.sh $1 && \
./gen_oclc.sh $1.ll
