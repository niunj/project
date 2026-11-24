#!/usr/bin/env bash
# Build the target with the same dependency graph as the desktop products.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$root/build_all.sh" voxel_target_sim "$@"
