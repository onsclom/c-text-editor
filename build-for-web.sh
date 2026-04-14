#!/bin/sh
set -e

mkdir -p web_dist

emcc src/main.c -Wall -Wextra -std=c99 \
  -Isrc \
  -sUSE_SDL=3 \
  -sALLOW_MEMORY_GROWTH \
  --shell-file shell.html \
  -O2 \
  -o web_dist/index.html

echo "Build complete: web_dist/index.html"
