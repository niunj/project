# 场景与地球数据约定

- 新场景、常规地形/目标/云天空验收默认使用台北新数据：Copernicus GLO-30 DSM（已转椭球高）与 2024-02-14 Sentinel-2B 影像。
- 80 × 80 km 场景入口为 `config/earth/taipei-80km.vscene.json`；区域地图为 `taipei-region.map.json`，常规地球入口为 `default.map.json`。
- 全球体素地球使用 `config/earth/global-voxel.map.json`：台北区域从同一新数据生成 TMS 覆盖，其他地区保留原有全球数据。不得通过修改原始粗数据目录实现覆盖。
- 本机新数据目录 `/home/mte/EarthData/Taipei80km`，部署时可用 `VSGRAYSIM_TAIPEI_DATA_ROOT` 指定；粗全球源由 `VSGRAYSIM_EARTH_DATA_ROOT` 指定。准备命令见 `docs/scene_design_guide.md`。
- `taipei-coarse.map.json`、`taipei-80km-coarse.vscene.json`、`global-coarse.map.json` 仅用于显式旧数据对照，不能作为常规场景默认输入。
- 用户要求保持现有体素架构；沿用占用、断裂、刚体、碰撞、传感器和检查点链路。提高显示精度不代表提高了物理破坏精度，也不得把插值称为新增真实细节。
- 场景效果验证使用地球测试程序实际 Vulkan 截图。同机位、同 UTC 对照；单独报告稳定渲染与毁伤更新耗时，不得用渲染耗时替代完整仿真性能。
- 10 m 影像不是建筑几何，30 m DSM 不是裸地 DTM。建筑轮廓/高度、道路宽度/层位缺少来源时，必须标明推定，不能称为实测模型。
