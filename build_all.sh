#!/usr/bin/env bash
# Build tracked engine sources; configure never downloads or silently falls back
# to an installed VSG/Rocky. See docs/build_dependencies.md for external SDKs.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRESET=source-release
BUILD_OVERRIDE="${BUILD_DIR:-}"
JOBS="$(nproc 2>/dev/null || echo 4)"
if [ "$JOBS" -gt 4 ]; then JOBS=4; fi
ALLOW_LOCAL_CHANGES=OFF
RUN_TESTS=0
TARGETS=()
CMAKE_ARGS=()

usage() {
    cat <<'HELP'
Usage: ./build_all.sh [target ...] [options]
  --preset NAME          source-release (default) or source-debug
  --build-dir PATH       Override preset build directory (keeps old build intact)
  --jobs N               Positive parallel job count (default at most 4)
  --allow-local-changes  Build intentionally edited vendored sources; report dirty
  --test                 Run headless regression after building all targets
  --reconfigure          Accepted for compatibility; configure always runs
  -- -DNAME=VALUE ...    Additional CMake configure arguments, e.g. SDK paths
For custom CMakeUserPresets use cmake --preset NAME and cmake --build --preset NAME.
HELP
}

need_value() {
    if [ "$#" -lt 2 ] || [ -z "$2" ]; then
        echo "Missing value for $1" >&2; exit 2
    fi
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --preset) need_value "$@"; PRESET="$2"; shift 2 ;;
        --build-dir) need_value "$@"; BUILD_OVERRIDE="$2"; shift 2 ;;
        --jobs) need_value "$@"; JOBS="$2"; shift 2 ;;
        --allow-local-changes) ALLOW_LOCAL_CHANGES=ON; shift ;;
        --test) RUN_TESTS=1; shift ;;
        --reconfigure) shift ;;
        --) shift; CMAKE_ARGS=("$@"); break ;;
        --*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
        *) TARGETS+=("$1"); shift ;;
    esac
done

case "$PRESET" in source-release|source-debug) ;; *)
    echo "Use source-release/source-debug here, or invoke your custom CMake preset directly." >&2; exit 2 ;;
esac
case "$JOBS" in ''|*[!0-9]*) echo "--jobs must be a positive integer" >&2; exit 2 ;; esac
if [ "$JOBS" -lt 1 ]; then echo "--jobs must be positive" >&2; exit 2; fi
if [ "$RUN_TESTS" -eq 1 ] && [ "${#TARGETS[@]}" -ne 0 ]; then
    echo "--test requires a complete build; omit target names." >&2; exit 2
fi
BUILD_PATH="${BUILD_OVERRIDE:-$SCRIPT_DIR/build/$PRESET}"
if [[ "$BUILD_PATH" != /* ]]; then BUILD_PATH="$PWD/$BUILD_PATH"; fi

VERIFY_ARGS=(--repo "$SCRIPT_DIR")
if [ "$ALLOW_LOCAL_CHANGES" = ON ]; then VERIFY_ARGS+=(--allow-local-changes); fi
python3 "$SCRIPT_DIR/tools/dependencies/verify_sources.py" "${VERIFY_ARGS[@]}"

cd "$SCRIPT_DIR"
env -u LD_LIBRARY_PATH cmake --preset "$PRESET" -B "$BUILD_PATH" \
    "${CMAKE_ARGS[@]}" \
    -DVSGRAYSIM_ALLOW_LOCAL_DEPENDENCY_CHANGES="$ALLOW_LOCAL_CHANGES"
BUILD_ARGS=(--build "$BUILD_PATH" --parallel "$JOBS")
if [ "${#TARGETS[@]}" -ne 0 ]; then BUILD_ARGS+=(--target "${TARGETS[@]}"); fi
env -u LD_LIBRARY_PATH cmake "${BUILD_ARGS[@]}"
AUDIT_ARGS=(--build-dir "$BUILD_PATH" --json-output "$BUILD_PATH/dependency-audit.json")
if [ "${#TARGETS[@]}" -ne 0 ]; then AUDIT_ARGS+=(--allow-unbuilt); fi
if [ "$ALLOW_LOCAL_CHANGES" = ON ]; then AUDIT_ARGS+=(--allow-local-changes); fi
python3 "$SCRIPT_DIR/tools/dependencies/audit_build.py" "${AUDIT_ARGS[@]}"

if [ "$RUN_TESTS" -eq 1 ]; then
    env -u LD_LIBRARY_PATH BUILD_DIR="$BUILD_PATH" "$SCRIPT_DIR/run_regress.sh" --skip-visual
fi
echo "Build complete: $BUILD_PATH"
