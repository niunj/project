# 海洋验证资产

默认海洋演示已改用 `models/CG-57/cg57.osgb` 的真尺寸高细节巡洋舰。本目录仅保留显式小艇性能/海底碰撞对照。

这些网格、颜色和沙底纹理由 `tools/earth/build_ocean_demo_assets.py` 确定性生成，供 `config/earth/taipei-coast-ocean-reference-boat.vscene.json` 对照验证。不是实测船舶、潜航器或台北海底数据。

- `test-vessel.osgb`：约 12 m 长的封闭测试船，体素化上限 128，测试体积平均密度 400 kg/m³。
- `test-submersible.osgb`：约 8 m 长的封闭潜航器，体素化上限 96，测试平均密度 1025 kg/m³。
- `assumed-seabed.osgb`：128 × 128 m 的人工起伏海底，顶面约 −14 m，底面 −18 m。
- OBJ 和 MTL 是可编辑源；OSGB 包含材质和纹理，用于引擎现有 OSG 导入链路。普通 OBJ 导入器目前不完整支持 MTL，因此场景引用 OSGB。

密度包含封闭体积的浮力假设，不是钢材密度。破坏后仍沿用剩余占用体素的质量/体积；尚未模拟进水舱室、压载或真实船舶稳性参数。

重建示例（需要 Pillow 和 OpenSceneGraph）：

```bash
env LD_LIBRARY_PATH=/home/mte/Code/osg_365_glx/lib \
  python3 tools/earth/build_ocean_demo_assets.py \
  --osgconv /home/mte/Code/osg_365_glx/bin/osgconv
```

运行：

```bash
build/gui/sim_earth_gui_tests --scene-ocean-demo
```

场景中的船具有初始航速，自动生成可衰减的近船扰动和泡沫尾迹。原生验收额外在 11 秒施加一个已记录的推进脉冲以观察尾流，它不是发动机标定。

按 1/2/3/4 切换水面、水线、水下和仰视机位，空格暂停。详细机制和验证范围见 `docs/scene_design_guide.md` 的海洋章节。
