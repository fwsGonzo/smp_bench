#!/usr/bin/env bash
FILE=${1:-build/smp_bench}
dd if=$FILE > /dev/tcp/10.0.0.42/1234
