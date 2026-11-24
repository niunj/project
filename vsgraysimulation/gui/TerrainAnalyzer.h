#pragma once
/// @file TerrainAnalyzer.h
/// @brief 纯算法地形分析引擎 (第二阶段专业增强)。
///
/// 提供与渲染无关的地形分析算法, 输入为高度场快照:
///   - row-major 布局, index = z * n + x, 单位米
///   - 采样点 (i, j) 对应地形局部坐标 (i * cell, j * cell),
///     cell = extent / (n - 1), 与 layer1_scene::TerrainSystem 一致
///
/// 分析能力:
///   - 坡度图 (Slope, 度) / 坡向图 (Aspect, 度)
///   - 高程统计 (最高 / 最低 / 平均)
///   - 两点通视分析 (LOS 采样判定 + 首个遮挡点)
///   - 两点剖面数据 (沿程距离-高程序列)
///   - 按高程 / 坡度的材质分类 (水 / 草地 / 岩石 / 雪)
///   - 坡度 / 坡向 / 材质 → RGB 颜色数组 (供 QImage / 纹理使用)
///
/// 本类不依赖 Qt / VSG, 便于单元测试与复用。

#include <cstdint>
#include <vector>

namespace vsgraysim { namespace gui {

class TerrainAnalyzer {
public:
    // ------------------------------------------------------------------
    // 结果结构
    // ------------------------------------------------------------------

    /// 高程统计 (米)。
    struct ElevationStats {
        double min_h  = 0.0;   ///< 最低高程
        double max_h  = 0.0;   ///< 最高高程
        double mean_h = 0.0;   ///< 平均高程
    };

    /// 两点通视 (LOS) 分析结果。
    struct LosResult {
        bool   visible    = true;    ///< 两点是否通视
        double blocker_x  = 0.0;     ///< 首个遮挡点地形局部坐标 X (米)
        double blocker_z  = 0.0;     ///< 首个遮挡点地形局部坐标 Z (米)
        double blocker_h  = 0.0;     ///< 遮挡点处地形高程 (米)
        double blocker_t  = 0.0;     ///< 遮挡位置线参数 t ∈ (0,1)
    };

    /// 剖面采样点: 距起点沿程距离 (米) + 地形高程 (米)。
    struct ProfileSample {
        double dist   = 0.0;
        double height = 0.0;
    };

    /// 按高程 / 坡度分类的材质类型。
    enum class Material {
        Water  = 0,   ///< h ≤ 水位
        Grass  = 1,   ///< 平缓且不高
        Rock   = 2,   ///< 坡度 ≥ 30°
        Snow   = 3    ///< 高程位于地形上部 1/4 且坡度 < 30°
    };

    // ------------------------------------------------------------------
    // 输入校验 / 通用采样
    // ------------------------------------------------------------------

    /// 高度场是否为合法的 n×n 网格。
    static bool validField(const std::vector<double>& heights, uint32_t n);

    /// 双线性采样地形局部坐标 (x, z) 处的高程, 坐标越界时钳制到边缘。
    static double sampleBilinear(const std::vector<double>& heights, uint32_t n,
                                 double extent, double x, double z);

    // ------------------------------------------------------------------
    // 高程统计
    // ------------------------------------------------------------------
    static ElevationStats elevationStats(const std::vector<double>& heights,
                                         uint32_t n);

    // ------------------------------------------------------------------
    // 坡度 / 坡向
    // ------------------------------------------------------------------

    /// 坡度图 (度, [0, 90]), 输出与 heights 同布局 (row-major n×n)。
    /// 中心差分, 网格边界使用单侧差分。
    static std::vector<double> slopeMap(const std::vector<double>& heights,
                                        uint32_t n, double extent);

    /// 坡向图 (度, [0, 360), 0°=北(-Z), 90°=东(+X), 180°=南, 270°=西;
    /// 坡向为下坡方向的罗盘方位角)。平地 (梯度≈0) 输出 -1。
    static std::vector<double> aspectMap(const std::vector<double>& heights,
                                         uint32_t n, double extent);

    // ------------------------------------------------------------------
    // 两点分析 (地形局部坐标)
    // ------------------------------------------------------------------

    /// 两点通视分析: 沿视线采样, 地形高程高于视线即遮挡。
    /// eye_lift / target_lift: 观察/目标点相对地表的抬升 (米, 如眼高 1.7)。
    /// samples: 视线内部采样数 (两端点不参与判定)。
    static LosResult lineOfSight(const std::vector<double>& heights,
                                 uint32_t n, double extent,
                                 double x0, double z0, double x1, double z1,
                                 double eye_lift, double target_lift,
                                 int samples);

    /// 两点剖面: 返回 samples+1 个采样点 (含起终点),
    /// dist 为距起点的水平沿程距离。
    static std::vector<ProfileSample> profile(const std::vector<double>& heights,
                                              uint32_t n, double extent,
                                              double x0, double z0,
                                              double x1, double z1,
                                              int samples);

    // ------------------------------------------------------------------
    // 材质分类 (按高程 / 坡度)
    // ------------------------------------------------------------------

    /// 分类规则:
    ///   Water : h ≤ water_level
    ///   Rock  : 坡度 ≥ 30° (陡崖)
    ///   Snow  : 归一化高程 ≥ 0.75 且坡度 < 30° (顶部缓坡积雪)
    ///   Grass : 其余
    static std::vector<Material> materialMap(const std::vector<double>& heights,
                                             uint32_t n, double extent,
                                             double water_level);

    // ------------------------------------------------------------------
    // 颜色输出 (RGB888, 行主序 n*n*3 字节, 可直接构造 QImage)
    // ------------------------------------------------------------------

    /// 坡度配色: 0° 绿 → 30° 黄 → 60° 橙 → 90° 深红。
    static std::vector<uint8_t> slopeColors(const std::vector<double>& slope_deg,
                                            uint32_t n);

    /// 坡向配色: 罗盘色环 N红 E绿 S蓝 W紫 (相邻方位平滑插值), 平地灰。
    static std::vector<uint8_t> aspectColors(const std::vector<double>& aspect_deg,
                                             uint32_t n);

    /// 材质配色: 水蓝 / 草绿 / 岩灰 / 雪白。
    static std::vector<uint8_t> materialColors(const std::vector<Material>& materials,
                                               uint32_t n);
};

}} // namespace vsgraysim::gui
