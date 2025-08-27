#!/bin/bash

gdb -q \
  -iex "set confirm off" \
  -iex "set breakpoint pending on" \
  -iex "b abort" \
  -iex "set environment PYTHONHOME=$WIN_BIN_DIR" \
  -iex "set environment PYTHONPATH=$WIN_PY_DIR;$WIN_PY_ZIP" \
  -ex run --args "$@"