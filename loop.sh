#!/bin/bash
set -e
make -j4

while true; do
	boot smp_bench
done
