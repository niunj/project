#!/usr/bin/env bash
# =============================================================================
# run_regress.sh —— vsgraysim 一键回归入口 (P0 工程化)。
#
# 顺序执行:
#   1) CTest 数值/调度/内核生命周期与 Vulkan compute 回归 (无窗口)
#   2) 三套 headless smoke (数值断言, 不依赖 X11/GPU):
#        env_smoke_sky / env_smoke_cloud / env_smoke_ocean
#   3) env_viewer 视觉回归矩阵 (X11 抓帧; 需 DISPLAY, 无则跳过并标记 SKIP):
#        env_modules/tools/env_regress.sh
#
# 用法: ./run_regress.sh [--skip-visual] [输出目录]
#   --skip-visual   跑 CTest + smoke, 跳过 X11 视觉矩阵 (CI/无显示环境)
#   [输出目录]      视觉回归截图/统计输出目录 (默认 /tmp/env_regress)
# 退出码: 0 = 全部 PASS; 非 0 = 存在 FAIL。
# =============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build/source-release}"
SKIP_VISUAL=0
OUT="/tmp/env_regress"

for arg in "$@"; do
    case "$arg" in
        --skip-visual) SKIP_VISUAL=1 ;;
        *) OUT="$arg" ;;
    esac
done

FAIL=0

run_smoke() {  # name binary
    local name="$1" bin="$2"
    if [ ! -x "$bin" ]; then
        echo "[FAIL] $name: 二进制不存在 ($bin) — 先构建对应 target"
        FAIL=1; return
    fi
    echo "---- smoke: $name ----"
    env -u LD_LIBRARY_PATH "$bin" > "/tmp/${name}.log" 2>&1
    local rc=$?
    if [ $rc -eq 0 ]; then
        echo "  [PASS] $name (日志 /tmp/${name}.log)"
    else
        echo "  [FAIL] $name exit=$rc (日志 /tmp/${name}.log)"
        FAIL=1
    fi
}

echo "===== 阶段 1/3: 数值 / 内核 / shader 回归 ====="
env -u LD_LIBRARY_PATH -u DISPLAY ctest --test-dir "$BUILD_DIR" \
    --output-on-failure --no-tests=error || FAIL=1

echo "===== 阶段 2/3: headless smoke ====="
run_smoke env_smoke_sky    "$BUILD_DIR/env_modules/env_smoke_sky"
run_smoke env_smoke_cloud  "$BUILD_DIR/env_modules/env_smoke_cloud"
run_smoke env_smoke_ocean  "$BUILD_DIR/env_modules/env_smoke_ocean"

echo ""
echo "===== 阶段 3/3: 视觉回归矩阵 ====="
if [ "$SKIP_VISUAL" -eq 1 ]; then
    echo "[SKIP] 已指定 --skip-visual"
elif [ -z "${DISPLAY:-}" ]; then
    echo "[SKIP] 无 DISPLAY (X11 抓帧不可用); 手动补跑: DISPLAY=:1 $SCRIPT_DIR/env_modules/tools/env_regress.sh"
elif grep -qx 'VSGRAYSIM_WITH_ROCKY:BOOL=OFF' "$BUILD_DIR/CMakeCache.txt"; then
    mkdir -p "$OUT"
    env -u LD_LIBRARY_PATH QT_QPA_PLATFORM=xcb "$BUILD_DIR/gui/sim_earth_gui_tests" --native-volume-capture "$OUT/voxel-earth.png" || FAIL=1
else
    env -u LD_LIBRARY_PATH ENV_VIEWER="${ENV_VIEWER:-$BUILD_DIR/env_modules/env_viewer}" DISPLAY="$DISPLAY" \
        "$SCRIPT_DIR/env_modules/tools/env_regress.sh" "$OUT" || FAIL=1
fi

echo ""
if [ $FAIL -eq 0 ]; then
    echo "===== 一键回归: 全部通过 ====="
else
    echo "===== 一键回归: 存在失败项 ====="
fi
exit $FAIL
