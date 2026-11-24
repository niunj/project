#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${BUILD_DIR:-"$root/build/source-release"}
exec env -u LD_LIBRARY_PATH gdb -batch -x "$root/gdb_cmds.txt" --args "$build_dir/app/vsgraysim_launcher" "$@"
