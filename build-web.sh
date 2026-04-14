#!/bin/sh
set -e

mkdir -p build-web

emcc src/main.c -Wall -Wextra -std=c99 \
  -Isrc \
  -sUSE_SDL=3 \
  -sALLOW_MEMORY_GROWTH \
  --shell-file shell.html \
  -O2 \
  -o build-web/index.html

echo "Build complete: build-web/index.html"
