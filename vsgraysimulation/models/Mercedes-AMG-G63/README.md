# 2025 Mercedes-AMG G 63

`mercedes-amg-g63.glb` 是 Ddiaz Design 发布的模型，保留内嵌纹理、材质和源节点。GLB 内含作者及 CC BY-NC-SA 4.0 声明，项目用它做本地非商业演示。派生部件沿用同一许可。[作者模型](https://sketchfab.com/models/f583b5bfc17346c08573dc4f1edebefe/embed) · [下载镜像](https://github.com/jiaxiantao/3d-car-viewing/blob/main/public/models/market/2025_mercedes-benz_g-class_amg_g_63.glb) · [许可](https://creativecommons.org/licenses/by-nc-sa/4.0/)

SHA-256 为 `01f76d3c94eaf1c2c30dec183ce33c6ba21eb8096d552b3ece58265a2f8195f4`。沿用已验收的比例、源表面体素化、透明玻璃和灯具贴图。统一体素边长为 0.024559380595649348 m。缩放按 2.89052 m 轴距校准。[奔驰尺寸](https://media.mbusa.com/news/2025-mercedes-amg-g-63-quick-reference-guide)

车身、四轮、前制动卡钳与左右前门用源网格拆分。前门外板、玻璃、门内板与后视镜附件跟随同一铰链。共用网格只拆分前门所属的三角面，保留 UV 和材质绑定。拆分保留原资产的 89,147 个三角面。车轮按轮心路径与轮胎半径求滚转，前轮内外转角分别计算，前卡钳仅随转向偏转。

从仓库根目录生成可在场景设计器和仿真主控打开的 `build/g63-motion/taipei-g63-motion.vtopic.json`。

```bash
python3 tools/prepare_g63_motion.py build/g63-motion

QT_QPA_PLATFORM=xcb VSGRAYSIM_G63_FRAMES=1 \
  build/source-refresh/gui/sim_earth_gui_tests --earth-g63-motion \
  build/g63-motion/taipei-g63-motion.vtopic.json build/g63-motion/captures
```

演示在台北新地球数据的试车台上使用预设低速轨迹和车门动作，状态来自正式体素世界的 pose track 与固定步。运行时动画以生成后的体素碰撞体更新，不需要另外挂载原模型。后门和尾门保留静态装配。资产修改及生成文件说明请随模型一起保留。
