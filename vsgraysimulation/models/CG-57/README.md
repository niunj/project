# CG-57 高细节巡洋舰

来源：[FlightGear fgdata / CG-57-high-detail](https://gitlab.com/flightgear/fgdata/-/tree/next/Models/Maritime/Military/CG-57-high-detail)。固定上游提交与每个源文件的 SHA-256 见 `provenance.json`；上游 GPL-2.0 许可文本保留于 `UPSTREAM-LICENSE.md`，完整原始 AC3D、贴图和 XML 位于 `source/`。本目录转换资产保留同一上游许可。

正式场景使用 `cg57.vsgb`。它由同一份原始三角面、贴图和材质数据重打包，继续使用相同的体素化设置。

`cg57.osgb` 由 `tools/earth/prepare_cg57.cpp` 旋转坐标、平移水平中心、嵌入原贴图，并按源节点名补记航行灯自发光材质得到，没有缩比或删减细节。约长171.913m、宽20.584m，垂向从−7.772m至43.779m；舰艏为局部+Z（上游bowwake位于−X），上为+Y，保留源模型名义水线Y=0。模型尺寸来自源几何，不是本项目对实舰的测量。

默认海洋演示使用这个模型，`max_dimension=512` 受现有每模型4M体素预算约束，自动选出 **0.3597343m** 体素，实际网格 **58×144×478**，占用 **811670** 格。这个分辨率不能冒称原12m小艇约0.094m的绝对物理精度。显示保留源几何重建的表面、法线与贴图；破坏仍基于上述占用单元。

带源角点重建的目标改用Bullet GImpact接触网格，直接使用体素系统发布的规范三角面，与显示和CPU测距一致；不是额外挂载原模型碰撞代理。质量与惯量仍来自体素占用，断裂/刚体/传感器/检查点链路保留。普通无源角点体素目标继续使用原精确复合盒。此前452配置受复合凸体数量限制的探测结果仅作为旧接触后端诊断，不再是默认分辨率。

平均密度167.30299kg/m³是按源名义水线下的体素排水量配平的**假定整舰体积平均密度**，不是钢材密度，也不是实舰的认证排水量、重心或稳性数据。原测试艇仅在显式 `*-reference-boat.vscene.json` 中保留，避免把模型规模变化冒充性能优化收益。

转换工具（需要OpenSceneGraph SDK；源文件已经随目录保留）：

```bash
g++ -std=c++17 -O2 tools/earth/prepare_cg57.cpp \
  -I/home/mte/Code/osg_365_glx/include -L/home/mte/Code/osg_365_glx/lib \
  -Wl,-rpath,/home/mte/Code/osg_365_glx/lib -losgDB -losg -lOpenThreads \
  -o build/ocean-natural/prepare_cg57
LD_LIBRARY_PATH=/home/mte/Code/osg_365_glx/lib \
  build/ocean-natural/prepare_cg57 models/CG-57/source/CG-57.ac models/CG-57/cg57.osgb
```

稳性限制：当前整舰仍采用均匀密度，缺少实舰低位机械、燃油和压载分布。实际动态验收出现明显侧倾；不得据此声称已达到实舰稳性。1920×1080细节验收在初始冻结姿态采集，动态路线另行保留。

坐标验收：FlightGear源模型的−X为舰艏，转换后舰艏为局部+Z、舰艉为−Z。固定动态试验的−Z初速/脉冲是倒航负载，尾流仍按实际速度生成，不将其描述为前进推进器模型。

联合专题 `config/topics/taipei-integrated.vtopic.json` 启用 `source_clipped` 高细节表面：源三角片按占用单元裁剪，避免细栏杆/桅杆共用一个吸附角点。占用/质量和0.3597343m破坏单元不变；显式细节模式有独立4M表面顶点预算，图集依旧受64MiB限制。场景类别线程池与CPU准备子进程用于加载，GPU负责实际云/水面/材质绘制。细节验收见 `build/scene-topic/accepted/`。
