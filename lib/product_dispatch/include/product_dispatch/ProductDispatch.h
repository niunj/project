#pragma once
/// @file ProductDispatch.h —— B5 分发时序桥接 (设计文档 §5/§12c-B5)。
///
/// 职责: 渲染链产品帧 (rocky_env ProductFrame) → 传输层帧 (vsgraysim
/// IrFrame/VisFrame/RadarFrame/SarFrame) 的**映射 + 门控分发** —— 渲染层
/// (rocky_env) 与数据面 (vsgraysim/broker) 之间的极小桥接, 仿真端与测试
/// 程序共享单一实现 (仿 lib/objloader 先例)。
///
/// 分发时序四要素 (B5):
///   1. ProductFrame→传输帧映射      —— 按 frame_kind/type 选帧型, 参数
///      从 SensorDesc 同名映射, 载荷直传 (pixels/iq 直映射, 无附加处理);
///   2. stages 发布                  —— SensorDesc.stages (§14.3) 决定发布级,
///      空缺省 {P4 image} (显控使用); 雷达五段名同映射到 P0..P4;
///   3. 链接门控                     —— 未链接 (显控未订阅) 不发布
///      ("不订阅不计算" 的发布侧执行; RT 生成侧门控在调度层);
///   4. 供电停帧 (eq/control)        —— PowerOff 后 dispatch 直接短路,
///      帧不再产出 (broker 通道 powered 为最终兜底)。
///
/// 依赖: rocky_env 公共头 (SensorApi.h/SceneSource.h) + vsgraysim 公共头
/// (SensorFrames.h), 全部为数据结构 —— 零符号链接, 纯头包含。
#ifndef VSGRAYSIM_PRODUCT_DISPATCH_H
#define VSGRAYSIM_PRODUCT_DISPATCH_H

#include <rocky_env/SensorApi.h>
#include <rocky_env/SceneSource.h>

#include <vsgraysim/broker/SensorFrames.h>

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace product_dispatch {

// ===========================================================================
// 映射: ProductFrame → 传输帧 (载荷直传; 参数按 SensorDesc 同名映射)
// ===========================================================================

/// 传感器描述 → 传输帧型选择 (frame_kind 决定; sar 类型 → SarFrame)。
enum class SinkKind { Ir, Vis, Radar, Sar, None };

/// frame_kind (+原始 type) → 传输帧型。laser 等未映射类型 → None。
SinkKind sinkKindFor(const rockyenv::sensor::SensorDesc& desc);

/// 帧标识公共段 (帧号/时统/标识; 各映射共用)。
void fillCommon(rockyenv::sensor::ProductFrame& pf,
                uint32_t sensor_id, uint32_t platform_id);

/// IR (SWIR/MWIR/LWIR): 载荷 = pixels (单位随 stage, 见 IrFrame.stage 注释)。
void mapToIr(const rockyenv::sensor::ProductFrame& pf,
             const rockyenv::sensor::SensorDesc& desc, vsgraysim::IrFrame& out);

/// VIS/TV: pixels (P0-P2 radiance, P3 full-well fraction, P4 normalized display).
void mapToVis(const rockyenv::sensor::ProductFrame& pf,
              const rockyenv::sensor::SensorDesc& desc, vsgraysim::VisFrame& out);

/// 雷达: 载荷 = iq (B6 前仅骨架; RD 图/回波形态随 stage)。
void mapToRadar(const rockyenv::sensor::ProductFrame& pf,
                const rockyenv::sensor::SensorDesc& desc, vsgraysim::RadarFrame& out);

/// SAR: 载荷 = iq (复回波/复图像)。
void mapToSar(const rockyenv::sensor::ProductFrame& pf,
              const rockyenv::sensor::SensorDesc& desc, vsgraysim::SarFrame& out);

// ===========================================================================
// ProductDispatcher —— 每传感器实例的分发器 (映射 + 三重门控 + 统计)
// ===========================================================================
struct DispatchStats {
    uint64_t published = 0;         ///< 已发布帧数 (过全部门控)
    uint64_t dropped_unlinked = 0;  ///< 链接门控丢弃 (未订阅)
    uint64_t dropped_unpowered = 0; ///< 供电门控丢弃 (断电停帧)
    uint64_t dropped_invalid = 0;   ///< missing or malformed payload / unsupported GPU reference
    uint64_t sink_rejected = 0;     ///< downstream did not accept the frame
    uint64_t dropped_stage = 0;     ///< stages 门控丢弃 (级未配置发布)
};

class ProductDispatcher {
public:
    /// 配置: stages 集合解析 (空缺省 {P4 image}); 帧型按 desc 确定。
    void configure(const rockyenv::sensor::SensorDesc& desc);

    /// 运行态级覆盖 (CLI/Param 优先级高于场景 JSON): "raw,image" 等。
    void setStagesFromCsv(const std::string& csv);

    /// 链接门控 (显控订阅状态; 每帧由调用方刷新)。
    void setLinked(bool v) { linked_ = v; }
    bool linked() const { return linked_; }

    /// 供电门控 (eq/control PowerOff → false; false 时停帧)。
    void setPowered(bool v) { powered_ = v; }
    bool powered() const { return powered_; }

    /// 已配置发布级 (升序)。
    const std::vector<rockyenv::sensor::ProductStage>& stages() const { return stages_; }
    SinkKind kind() const { return kind_; }

    /// 发布级是否被配置 (调用方据此决定是否读回该级)。
    bool stageEnabled(rockyenv::sensor::ProductStage s) const;

    // ---- 发布 sink (调用方接到 SimDataBroker::publish*; 按帧型接一个) ----
    std::function<bool(const vsgraysim::IrFrame&)>    onIr;
    std::function<bool(const vsgraysim::VisFrame&)>   onVis;
    std::function<bool(const vsgraysim::RadarFrame&)> onRadar;
    std::function<bool(const vsgraysim::SarFrame&)>   onSar;

    /// 分发一帧: stages 门控 → 链接门控 → 供电门控 → 映射 → sink。
    /// 返回是否发布。
    bool dispatch(const rockyenv::sensor::ProductFrame& pf);

    const DispatchStats& stats() const { return stats_; }

private:
    rockyenv::sensor::SensorDesc desc_{};
    SinkKind kind_ = SinkKind::None;
    std::vector<rockyenv::sensor::ProductStage> stages_;
    std::set<uint32_t> stageSet_;
    bool linked_ = false;
    bool powered_ = true;
    DispatchStats stats_{};
};

} // namespace product_dispatch

#endif // VSGRAYSIM_PRODUCT_DISPATCH_H
