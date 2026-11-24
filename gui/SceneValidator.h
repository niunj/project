#pragma once
/// @file SceneValidator.h
/// @brief 场景一致性验证 (第二阶段专业增强)。
///
/// 检查项
/// ------
///   1. 传感器参数范围: wavelength / beam_width / power / pulse_rate /
///      duty_cycle / fov_h / fov_v 等取值合法性;
///   2. 对象航迹: 航路点时间是否严格递增;
///   3. 对象高度: 对象 Y 坐标是否低于地形表面 (加载了地形时);
///   4. 传感器安装平台: platform 引用的实体是否存在。
///
/// 验证结果以模态 QDialog 报告 (通过/警告/错误列表)。

#include <QString>
#include <QWidget>
#include <vector>

#include "SceneDesignerWindow.h"

namespace vsgraysim { class TerrainSystem; }

namespace vsgraysim { namespace gui {

/// 无状态场景验证器。
class SceneValidator {
public:
    enum class Severity { Pass, Warning, Error };

    /// 一条验证结论。
    struct Issue {
        Severity severity;
        QString  message;
    };

    /// 验证结果集合。
    struct Result {
        std::vector<Issue> issues;
        int errorCount() const;
        int warningCount() const;
        int passCount() const;
    };

    /// 对整个场景执行验证。`terrain` 可为空 (无地形时跳过高度检查)。
    static Result validate(const std::vector<SceneObject>& objects,
                           const TerrainSystem* terrain);

    /// 以模态对话框显示验证报告。
    static void showReport(QWidget* parent, const Result& result);
};

}} // namespace vsgraysim::gui
