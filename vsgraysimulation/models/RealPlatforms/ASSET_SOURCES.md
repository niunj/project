# 外观资产来源

- Skywalker X8 使用 David van Wyk 的 Gazebo 飞翼机体、副翼和推桨网格。保留作者的 BSD-3-Clause 许可。场景只引用网格资源。[上游模型](https://github.com/davidvanwyk/SkywalkerX8_gazebo)
- 3DR Iris 使用 Thomas Gubler 与 Lorenz Meier 的 PX4 仿真网格，包括机体 STL 和两种旋向的桨叶 DAE。随文件保留 BSD-3-Clause 许可。[PX4 型号说明](https://github.com/PX4/PX4-SITL_gazebo-classic/blob/main/models/iris/model.config) · [资产许可说明](https://pegasussimulator.github.io/PegasusSimulator/source/references/license.html#dr-iris-asset)
- Falcon 9 一级与上面级外观由 Space Radar 制作，MIT 许可。将其压缩 GLB 解码成 OBJ，保持外表面和色槽，并把上面级与两半整流罩导出为单独资产。[一级模型](https://models.spaceradar.ai/falcon9-stage1/) · [上面级模型](https://models.spaceradar.ai/falcon9-upper/)
- SWOT 来自 NASA/JPL-Caltech 公开三维模型。将其转成 OBJ，并将左右太阳阵列从展开网格中拆出，按各自的铰接基准导入场景。Courtesy NASA/JPL-Caltech。[原始三维模型](https://swot.jpl.nasa.gov/resources/86/swot-3d-model/) · [太阳翼展开实录](https://swot.jpl.nasa.gov/resources/187/swot-deploys-solar-arrays/)

Falcon 9 实际曾在范登堡发射 SWOT。仓库中的台北场景属于压缩时间的展示，场景中的位置、姿态和分离时刻由演示关键帧提供。[NASA/JPL 任务记录](https://swot.jpl.nasa.gov/resources/186/international-swot-mission-launches-from-vandenberg-space-force-base-launch-recap/)
