#!/bin/bash
. "common.sh"

# Compile external utilities
gcc extractsymbols.c -lelf -o extractsymbols
chmod +x extractsymbols

# Set-up needed directories
mkdir -p obj-intel64

# Debug release
make obj-intel64/Linter.so DEBUG=1 -B
mv obj-intel64/Linter.so obj-intel64/Linter-debug.so

# Production release
make obj-intel64/Linter.so -B
