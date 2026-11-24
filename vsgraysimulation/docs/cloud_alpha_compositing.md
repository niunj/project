# 云与海面倒影的白边修复

本轮只修正透明合成状态，保留单云对象、密度、光照参数、时间重投影、海洋波场、浮力与碰撞精度。

原来复用 VSG 的默认透明混合时，Alpha 系数是 `SRC_ALPHA / ZERO`，导致结果 Alpha 等于源 Alpha 的平方。云的 RGB 已经与天空合成，但云边像素仍携带很低的 Alpha。例如实际 Vulkan 截图的 `(300,160)` 像素为 `(150,185,217,38)`：再次显示到白色背景时会被额外漂白，形成白圈。天空不应在画过云后重新变成透明。

统一使用 `weatherLayerBlend()`：颜色保留正常的直通透明合成，Alpha 使用 `ONE / ONE_MINUS_SRC_ALPHA`。云直绘、时间重投影后的最终合成、天空雾及地球远海都保留已有背景覆盖率。预乘 HDR 反射使用同一 Alpha 规则及 `ONE` 颜色源系数；其背景 Alpha 为 0，结果 Alpha 表示真实云覆盖率，供日月高光遮蔽使用，避免把覆盖率平方后过多漏光。

修复发生在 Vulkan 渲染管线，没有对输出 PNG 做填白、去边或后期展平。没有通过修改云颜色、密度或调暗光照掩盖白边；正常受光的亮边仍会存在。

## 验证

- 实际 GPU 回读验证生产混合状态：预乘 HDR 云覆盖率保持为 Alpha，而非 Alpha²；普通云画在不透明天空上后，结果 Alpha 保持为 1。
- 恒定辐射的合成边界在各种云覆盖率下保持恒定，验证不产生额外白边且 HDR 未提前截断。
- 台北新数据、场景时间 0、同机位，固定 2026-09-10 04:00 与 09:30 UTC，分别拍摄低视角海面、俯视海面、天空及日落。默认时间重投影和直接渲染回退都验证。
- 比较原始 RGBA 的天空覆盖率以及再次与白底合成所产生的额外亮度。该指标仅验证重复透明合成，不代表云真实性评分。

```sh
VSGRAYSIM_OCEAN_WEATHER_PASS=1 VSGRAYSIM_CLOUD_HALO_PASS=1 \
  build/gui/sim_earth_gui_tests --scene-ocean-capture build/cloud-halo/after
VSGRAYSIM_CLOUD_TEMPORAL=0 VSGRAYSIM_OCEAN_WEATHER_PASS=1 VSGRAYSIM_CLOUD_HALO_PASS=1 \
  build/gui/sim_earth_gui_tests --scene-ocean-capture build/cloud-halo/after-direct
python3 tools/compare_cloud_halo.py
```

原始截图及结果见 `build/cloud-halo/comparison.json`。这里记录的是冻结场景的稳定渲染，不重复冒充完整仿真性能测试。

## 本机实测结果

2026-11-22，960 × 540 原生 Vulkan，五个机位/时间组合各验证默认时间重建及关闭重建两条路径，共十组原始截图。修复后所有受检区域的 Alpha 都为 255；例如白天天空机位的异常非不透明像素从 151135 个变为 0，额外白底合成亮度偏移从 9.856/255 降为 0。白天和日落天空裁剪区的 RGB 与基线逐像素完全相同，确认没有通过调色或调暗云边改变外观。

默认时间重建路径的稳定渲染中位数如下，每项采样 15 帧，包含提交及 GPU 等待，不含地形加载、浮力或物理步进。短采样仅用于观察本次合成修复是否引入明显开销。

| 机位 | 修复前 ms | 修复后 ms |
| --- | ---: | ---: |
| 低视角海面，白天 | 14.786 | 14.753 |
| 俯视海面，白天 | 14.676 | 14.742 |
| 天空，白天 | 24.173 | 24.155 |
| 天空，日落 | 19.565 | 20.228 |
| 低视角海面，日落 | 11.135 | 11.263 |

`voxel_ocean_gpu_parity`、`earth_cloud_ocean_consistency`、`cloud_temporal_reprojection_gpu` 三项回归及 `env_smoke_cloud` 全部通过。GPU 回读测试直接使用生产管线的混合状态，分别覆盖普通与预乘 HDR 合成。
