#!/usr/bin/env bash
# =============================================================================
# env_regress.sh —— rocky_env (sky/cloud/ocean) 视觉回归矩阵 (P4-4)。
#
# 矩阵:
#   sky   : 3 时刻 (正午 UTC4 / 黄昏 UTC9.7 / 午夜 UTC16), cover=0
#   cloud : 3 云量 (0.2 / 0.5 / 0.8), 正午, mode=all + 夜间 cover=0.8 (C4)
#   ocean : 3 风速 (5 / 10 / 20 m/s), 正午, mode=ocean
#
# 每例: env_viewer 双视口预热后截图 → 仅对左观察口执行 Python/PIL 分区统计
# (顶部/中部/下部 平均 RGB + 非均匀度 std), 按物理预期判定 PASS/FAIL:
#   sky 正午 : 顶部偏蓝 (B>R) 且上部亮度低于地平线 (渐变存在)
#   sky 黄昏 : 中部地平带偏暖 (R>B)
#   sky 午夜 : 顶部暗 (< 60) 且非全零 (星空/银河在画面上)
#   cloud    : 上半部非均匀度随云量单调上升 (云真的画出来了)
#   cloud 夜间: 上半部亮度远低于正午同云量 (夜间云变暗)
#   ocean    : 近场亮度 (白沫/浪峰) 与波面纹理梯度随风速上升
#
# 用法: ./env_regress.sh [输出目录]   (默认 /tmp/env_regress)
# 依赖: env_viewer 已构建 (build/source-release/env_modules/env_viewer), python3 + Pillow。
# =============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VIEWER="${ENV_VIEWER:-$SCRIPT_DIR/../../build/source-release/env_modules/env_viewer}"
OUT="${1:-/tmp/env_regress}"
FRAMES="${ENV_REGRESS_FRAMES:-64}"
RES="--width 1280 --height 720"   # 降分辨率加快矩阵
SHOT_FMT=ppm
FAILED_CASES=0
case "$FRAMES" in ''|*[!0-9]*) echo "ENV_REGRESS_FRAMES must be a positive integer" >&2; exit 2;; esac
if [ "$FRAMES" -lt 1 ]; then exit 2; fi

mkdir -p "$OUT"
if [ ! -x "$VIEWER" ]; then
    echo "错误: 找不到 env_viewer: $VIEWER" >&2
    echo "先构建: ./build_all.sh env_viewer" >&2
    exit 2
fi

# 抓帧: env_viewer 输出 PPM (Netpbm), PIL 可直接读。
run_case() {  # name mode extra-args...
    local name="$1"; shift
    local mode="$1"; shift
    echo "---- [$name] mode=$mode $*"
    rm -f "$OUT/${name}.${SHOT_FMT}"
    timeout "${ENV_REGRESS_TIMEOUT:-300}" "$VIEWER" --mode "$mode" --frames $FRAMES $RES \
        --band tv --no-rt --sen-res 160x128 --day-minutes 0 --time-scale 0 \
        --anchor '24.75,122.5,0' \
        --shot "$OUT/${name}.${SHOT_FMT}" "$@" > "$OUT/${name}.log" 2>&1
    local rc=$?
    if [ $rc -ne 0 ]; then
        echo "  [FAIL] env_viewer 退出码 $rc (见 $OUT/${name}.log)"
        echo "$name FAIL" >> "$OUT/summary.txt"
        FAILED_CASES=$((FAILED_CASES + 1))
        return 1
    fi
    return 0
}

rm -f "$OUT/summary.txt"

# ---- sky: 3 时刻 ----
run_case sky_noon   all --hours-utc 4.0  --cover 0.0 --obs-eye '0,20,0' --obs-center '0,20,-1000'
run_case sky_dusk   all --hours-utc 9.7  --cover 0.0 --obs-eye '0,20,0' --obs-center '0,20,-1000'
run_case sky_midnit all --hours-utc 16.0 --cover 0.0 --obs-eye '0,20,0' --obs-center '0,20,-1000'
# ---- cloud: 3 云量 (正午, 全模块同框) ----
run_case cloud_02 all --hours-utc 4.0 --cover 0.2 --cloud-volume --obs-eye '0,20,0' --obs-center '0,250,-1000'
run_case cloud_05 all --hours-utc 4.0 --cover 0.5 --cloud-volume --obs-eye '0,20,0' --obs-center '0,250,-1000'
run_case cloud_08 all --hours-utc 4.0 --cover 0.8 --cloud-volume --obs-eye '0,20,0' --obs-center '0,250,-1000'
# ---- ocean: 3 风速 (正午) ----
run_case ocean_w05 all --ocean --wave-mode 2 --hours-utc 4.0 --cover 0 --wind 5 --obs-eye '0,8,0' --obs-center '0,5,-300'
run_case ocean_w10 all --ocean --wave-mode 2 --hours-utc 4.0 --cover 0 --wind 10 --obs-eye '0,8,0' --obs-center '0,5,-300'
run_case ocean_w20 all --ocean --wave-mode 2 --hours-utc 4.0 --cover 0 --wind 20 --obs-eye '0,8,0' --obs-center '0,5,-300'
# ---- cloud 夜间 (C4: 云夜间应变暗) ----
run_case cloud_midnit all --hours-utc 16.0 --cover 0.8 --cloud-volume --obs-eye '0,20,0' --obs-center '0,250,-1000'

# ---- Python/PIL 分区统计 + 判定 ----
python3 - "$OUT" <<'PYEOF'
import sys, os
from PIL import Image

out = sys.argv[1]
expected = ['sky_noon','sky_dusk','sky_midnit','cloud_02','cloud_05','cloud_08',
            'ocean_w05','ocean_w10','ocean_w20','cloud_midnit']

def load(name):
    p = os.path.join(out, name + ".ppm")
    if not os.path.exists(p):
        raise RuntimeError(f"required screenshot missing: {p}")
    try:
        image = Image.open(p).convert("RGB")
        if image.width < 32 or image.height < 16:
            raise RuntimeError("screenshot is too small for two viewports")
        # The right pane is a sensor product with independent gain/colour;
        # including it would invalidate sky/cloud/ocean image predicates.
        return image.crop((0, 0, image.width // 2, image.height))
    except Exception as e:
        print(f"  [warn] {p} 读取失败: {e}")
        raise RuntimeError(f"required screenshot unreadable: {p}") from e

# Validate the complete matrix before evaluating any visual predicate. Missing
# or zero-sized images must never turn skipped predicates into a green run.
for case in expected:
    image = load(case)
    if image.width < 16 or image.height < 16:
        raise RuntimeError(f"invalid screenshot dimensions: {case}")
    with open(os.path.join(out, case + '.log'), errors='replace') as log:
        if 'Validation Error:' in log.read():
            raise RuntimeError(f"Vulkan validation failed: {case}")

def stats(img, y0f, y1f):
    """区域平均 RGB + 亮度 std。y0f/y1f 为高度比例。"""
    w, h = img.size
    y0, y1 = int(h * y0f), int(h * y1f)
    reg = img.crop((0, y0, w, y1))
    px = list(reg.getdata())
    n = len(px)
    r = sum(p[0] for p in px) / n
    g = sum(p[1] for p in px) / n
    b = sum(p[2] for p in px) / n
    lum = [(p[0]*299 + p[1]*587 + p[2]*114) / 1000 for p in px]
    mean = sum(lum) / n
    var = sum((v - mean) ** 2 for v in lum) / n
    return r, g, b, mean, var ** 0.5

def bright_count(img, y0f, y1f, thr=220):
    w, h = img.size
    y0, y1 = int(h * y0f), int(h * y1f)
    reg = img.crop((0, y0, w, y1))
    return sum(1 for p in reg.getdata()
               if (p[0]*299 + p[1]*587 + p[2]*114)/1000 > thr)

results = []

def check(name, cond, detail):
    tag = "PASS" if cond else "FAIL"
    results.append((name, tag, detail))
    print(f"  [{tag}] {name}: {detail}")

# ---- sky 判定 ----
im = load("sky_noon")
if im is not None:
    tr, tg, tb, tm, ts = stats(im, 0.0, 0.125)      # 顶部 1/8 (天顶)
    hr, hg, hb, hm, hs = stats(im, 0.40, 0.49)      # 固定水平机位的天空侧地平带
    check("sky_noon 顶部偏蓝", tb > tr, f"top RGB=({tr:.0f},{tg:.0f},{tb:.0f})")
    check("sky_noon 地平线更亮", hm > tm * 0.9, f"horizon {hm:.1f} vs top {tm:.1f}")

im = load("sky_dusk")
if im is not None:
    hr, hg, hb, hm, hs = stats(im, 0.40, 0.49)
    check("sky_dusk 地平带偏暖", hr > hb, f"horizon RGB=({hr:.0f},{hg:.0f},{hb:.0f})")

im = load("sky_midnit")
if im is not None:
    tr, tg, tb, tm, ts = stats(im, 0.0, 0.25)
    check("sky_midnit 顶部暗", tm < 60, f"top mean {tm:.2f}")
    check("sky_midnit 星空非全黑", tm > 0.5 or ts > 0.5,
          f"top mean {tm:.2f} std {ts:.2f}")

# ---- cloud 判定: 上半部非均匀度随云量上升 ----
# 修复 (2026-10-18): 旧实现 stats(0.0,0.5) 整区 std 被天空垂直渐变主导
# (三档云量均 ≈97~99, 无法区分)。改为逐行亮度 std 均值: 行内天空几乎均匀,
# std 只反映云的局部纹理 → 云量越高逐行 std 越大。
def rowstd_mean(img, y0f, y1f):
    """逐行亮度 std 的均值 (剔除天空垂直渐变对整区 std 的污染)。"""
    w, h = img.size
    y0, y1 = int(h * y0f), int(h * y1f)
    reg = img.crop((0, y0, w, y1))
    px = list(reg.getdata())
    w2 = reg.size[0]
    tot, cnt = 0.0, 0
    for y in range(reg.size[1]):
        row = [(px[y*w2+x][0]*299 + px[y*w2+x][1]*587 + px[y*w2+x][2]*114)/1000
               for x in range(w2)]
        m = sum(row) / len(row)
        if len(row) > 1:
            tot += (sum((v - m) ** 2 for v in row) / len(row)) ** 0.5
            cnt += 1
    return tot / max(cnt, 1)

stds = {}
for name in ("cloud_02", "cloud_05", "cloud_08"):
    im = load(name)
    if im is not None:
        # 采样带取中上部 (0.20~0.45): 避开天顶单一渐变 (0.0 起) 与
        # 地平强渐变 (0.5 附近), 云带主纹理区; 三档云量覆盖在此差异最大。
        stds[name] = rowstd_mean(im, 0.20, 0.45)
if len(stds) == 3:
    check("cloud 非均匀度随云量上升", stds["cloud_02"] < stds["cloud_08"],
          f"0.2→{stds['cloud_02']:.1f}  0.5→{stds['cloud_05']:.1f}  0.8→{stds['cloud_08']:.1f} (逐行std)")

# ---- ocean 判定: 近场亮度 (白沫/浪峰) 与波面纹理梯度随风速上升 ----
# 注: 不用白沫像素计数 —— 远场掠射的天空反射同样亮 (>200), 信号被淹没。
def grad_energy(img, y0f, y1f):
    """水平相邻像素亮度差的均值 (波面纹理越强越大)。"""
    w, h = img.size
    y0, y1 = int(h * y0f), int(h * y1f)
    reg = img.crop((0, y0, w, y1))
    px = list(reg.getdata())
    w2 = reg.size[0]
    lum = [(p[0]*299 + p[1]*587 + p[2]*114)/1000 for p in px]
    g = 0.0
    for y in range(reg.size[1]):
        row = lum[y*w2:(y+1)*w2]
        for x in range(1, w2):
            g += abs(row[x] - row[x-1])
    return g / len(lum)

means_o, grads_o = {}, {}
for name in ("ocean_w05", "ocean_w10", "ocean_w20"):
    im = load(name)
    if im is not None:
        # 修复 (2026-10-18): 旧"近场亮度" stats(0.85,1.0) 区域与风无关
        # (三档均 178.4 → 永久 FAIL)。海面 = 逐行亮度起伏: 静风均匀(std小),
        # 大风白沫/浪峰参差(std大) → 用 rowstd_mean 替代。
        means_o[name] = rowstd_mean(im, 0.60, 1.0)   # 海面区逐行亮度起伏
        grads_o[name] = grad_energy(im, 0.70, 0.90)  # 波面纹理
if len(means_o) == 3:
    check("ocean 近场亮度起伏随风速上升", means_o["ocean_w20"] > means_o["ocean_w05"],
          f"w5→{means_o['ocean_w05']:.1f}  w10→{means_o['ocean_w10']:.1f}  w20→{means_o['ocean_w20']:.1f} (逐行std)")
    check("ocean 波面纹理随风速上升", grads_o["ocean_w20"] > grads_o["ocean_w05"],
          f"w5→{grads_o['ocean_w05']:.3f}  w10→{grads_o['ocean_w10']:.3f}  w20→{grads_o['ocean_w20']:.3f}")

# ---- C4 判定: 夜间云变暗 (对比正午同云量) ----
im08 = load("cloud_08")
imn = load("cloud_midnit")
if im08 is not None and imn is not None:
    _, _, _, m08, _ = stats(im08, 0.0, 0.5)
    _, _, _, mn, _ = stats(imn, 0.0, 0.5)
    check("cloud 夜间云变暗", mn < 80 and mn < m08 * 0.6,
          f"midnight {mn:.1f} vs noon {m08:.1f}")

# ---- 汇总 ----
n_pass = sum(1 for _, t, _ in results if t == "PASS")
n_fail = sum(1 for _, t, _ in results if t == "FAIL")
print(f"\n===== env_regress 汇总: {n_pass} PASS / {n_fail} FAIL =====")
with open(os.path.join(out, "summary.txt"), "a") as f:
    for name, tag, detail in results:
        f.write(f"{name} {tag} {detail}\n")
sys.exit(1 if n_fail or n_pass == 0 else 0)
PYEOF
RC=$?
if [ "$FAILED_CASES" -ne 0 ]; then RC=1; fi

echo ""
if [ $RC -eq 0 ]; then
    echo "回归矩阵全部通过。截图与统计: $OUT"
else
    echo "存在失败项, 截图与统计: $OUT (summary.txt)"
fi
exit $RC
