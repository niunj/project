# 65 米级 CC0 柴电潜艇

`diesel-submarine.glb` 是 Innerscene 的原创通用柴电潜艇，按真实尺寸比例建模。保留下载的原始 GLB，包括艇体、围壳、舵面和螺旋桨，以及随文件提供的 PBR 贴图。许可为 CC0 1.0。[资产来源](https://www.innerscene.com/tools/library/3d-parts/diesel-submarine-33bd11e9) · [许可文本](https://creativecommons.org/publicdomain/zero/1.0/)

从 GLB 的位置数据测得全长 **65.2653 m**、含舵最大宽 **10.3000 m**、含桅高 **12.9000 m**。艇壳主体长 **64.0 m**、直径 **6.30 m**。文件坐标为米，Y 向上，艇首朝 +X，艇底位于 Y=0。场景按 1:1 导入。

`tools/prepare_sonar_crossing.py --diesel <输出目录>` 从原始 GLB 拆出艇身和螺旋桨，保留原始几何、贴图及材质。桨毂中心位于艇身局部 `(-0.65, 3.15, 0)` 米，旋转轴为 +X。艇身单元约 17 厘米，桨单元 4 厘米。演示按仿真时钟从静止加速至 90 rpm。

SHA-256 `1641a433b6d4fcdbc9146aa4a781de2f7f2b902243003168b47e0813b8652d0e`
