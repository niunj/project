/// @file SceneValidator.cpp
/// @brief 场景一致性验证实现 (参数范围 / 航迹时间 / 地形高度 / 安装平台)。

#include "SceneValidator.h"
#include "base/FlightConfiguration.h"

#include "vsgraysim/layer1_scene/TerrainSystem.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vsgraysim { namespace gui {

namespace {
/// tr() 语义的翻译入口 (非 QObject 上下文)。
inline QString tr_sv(const char* s)
{
    return QCoreApplication::translate("SceneValidator", s);
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// Result counters
// ---------------------------------------------------------------------------
int SceneValidator::Result::errorCount() const
{
    int n = 0;
    for (const auto& i : issues)
        if (i.severity == Severity::Error) ++n;
    return n;
}

int SceneValidator::Result::warningCount() const
{
    int n = 0;
    for (const auto& i : issues)
        if (i.severity == Severity::Warning) ++n;
    return n;
}

int SceneValidator::Result::passCount() const
{
    int n = 0;
    for (const auto& i : issues)
        if (i.severity == Severity::Pass) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
SceneValidator::Result SceneValidator::validate(const std::vector<SceneObject>& objects,
                                                const TerrainSystem* terrain)
{
    Result r;

    if (objects.empty())
        r.issues.push_back({Severity::Warning, tr_sv("场景中没有任何实体")});

    // 实体 id 集合 (安装平台存在性检查用)。
    std::vector<QString> ids;
    ids.reserve(objects.size());
    for (const auto& o : objects) ids.push_back(o.id);
    const auto id_exists = [&ids](const QString& id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };

    for (const auto& object : objects) if (object.platform_cat == 1) {
        const auto error = validateFlightConfiguration(object.motion_params);
        if (!error.isEmpty()) r.issues.push_back({Severity::Error, object.id + ": " + error});
        if (object.motion_authority == QStringLiteral("kinematic"))
            r.issues.push_back({Severity::Warning, object.id + tr_sv(": 规定轨迹平台不可由显控接管；需选择动力学运动方式")});
    }

    // ---- 1. 传感器参数范围 ----
    {
        int n_sensors = 0, n_bad = 0;
        for (const auto& o : objects) {
            int sn = 0;
            for (const auto& s : o.sensors) {
                ++n_sensors; ++sn;
                const QString tag = QString("%1#%2").arg(o.id).arg(sn);
                auto add_err = [&](const QString& msg) {
                    r.issues.push_back({Severity::Error, tag + ": " + msg});
                    ++n_bad;
                };
                if (s.wavelength <= 0.0)
                    add_err(tr_sv("波长必须大于 0 (当前 %1)").arg(s.wavelength));
                if (s.beam_width <= 0.0)
                    add_err(tr_sv("波束宽度必须大于 0 (当前 %1)").arg(s.beam_width));
                if (s.power < 0.0)
                    add_err(tr_sv("发射功率不能为负 (当前 %1)").arg(s.power));
                if (s.pulse_rate < 0.0)
                    add_err(tr_sv("脉冲重频不能为负 (当前 %1)").arg(s.pulse_rate));
                if (s.duty_cycle < 0.0 || s.duty_cycle > 1.0)
                    add_err(tr_sv("占空比必须在 [0,1] 范围内 (当前 %1)").arg(s.duty_cycle));
                if (s.fov_h <= 0.0 || s.fov_h > 360.0)
                    add_err(tr_sv("水平视场角必须在 (0,360] 范围内 (当前 %1)").arg(s.fov_h));
                if (s.fov_v <= 0.0 || s.fov_v > 180.0)
                    add_err(tr_sv("垂直视场角必须在 (0,180] 范围内 (当前 %1)").arg(s.fov_v));
                if (s.range_m <= 0.0)
                    add_err(tr_sv("作用距离必须大于 0 (当前 %1)").arg(s.range_m));
            }
        }
        if (n_sensors == 0)
            r.issues.push_back({Severity::Warning, tr_sv("场景中没有任何传感器")});
        else if (n_bad == 0)
            r.issues.push_back({Severity::Pass,
                tr_sv("传感器参数范围: %1 个传感器全部正常").arg(n_sensors)});
    }

    // ---- 2. 航迹时间递增 ----
    {
        int n_trails = 0, n_bad = 0;
        for (const auto& o : objects) {
            if (o.waypoints.size() < 2) continue;
            ++n_trails;
            for (size_t i = 1; i < o.waypoints.size(); ++i) {
                if (o.waypoints[i].time <= o.waypoints[i - 1].time) {
                    r.issues.push_back({Severity::Error,
                        tr_sv("%1: 航路点 %2 的时间 %3 未大于前一航路点时间 %4")
                            .arg(o.id).arg(int(i) + 1)
                            .arg(o.waypoints[i].time, 0, 'f', 2)
                            .arg(o.waypoints[i - 1].time, 0, 'f', 2)});
                    ++n_bad;
                }
            }
        }
        if (n_bad == 0)
            r.issues.push_back({Severity::Pass,
                n_trails == 0
                    ? tr_sv("航迹时间: 未定义航迹 (跳过)")
                    : tr_sv("航迹时间: %1 条航迹的时间序列全部递增").arg(n_trails)});
    }

    // ---- 3. 对象高度 vs 地形 ----
    {
        if (terrain) {
            int n_bad = 0;
            for (const auto& o : objects) {
                const double h = terrain->heightAtWorld(o.pos[0], o.pos[2]);
                if (o.pos[1] < h - 0.5) {
                    r.issues.push_back({Severity::Warning,
                        tr_sv("%1: 高度 %2 低于地形表面 %3")
                            .arg(o.id)
                            .arg(o.pos[1], 0, 'f', 2)
                            .arg(h, 0, 'f', 2)});
                    ++n_bad;
                }
            }
            if (n_bad == 0)
                r.issues.push_back({Severity::Pass,
                    tr_sv("对象高度: 全部位于地形表面之上")});
        } else {
            r.issues.push_back({Severity::Pass, tr_sv("对象高度: 未加载地形 (跳过)")});
        }
    }

    // ---- 4. 传感器安装平台存在性 ----
    {
        int n_bad = 0;
        for (const auto& o : objects) {
            int sn = 0;
            for (const auto& s : o.sensors) {
                ++sn;
                if (s.platform.isEmpty()) continue;
                if (!id_exists(s.platform)) {
                    r.issues.push_back({Severity::Error,
                        tr_sv("%1#%2: 安装平台 '%3' 不存在")
                            .arg(o.id).arg(sn).arg(s.platform)});
                    ++n_bad;
                }
            }
        }
        if (n_bad == 0)
            r.issues.push_back({Severity::Pass, tr_sv("传感器安装平台: 全部有效")});
    }

    return r;
}

// ---------------------------------------------------------------------------
// Report dialog
// ---------------------------------------------------------------------------
void SceneValidator::showReport(QWidget* parent, const Result& result)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr_sv("场景验证报告"));
    dlg.resize(600, 440);

    auto* v = new QVBoxLayout(&dlg);

    const int errs  = result.errorCount();
    const int warns = result.warningCount();
    const int passes = result.passCount();
    auto* summary = new QLabel(&dlg);
    summary->setText(QString("<b>%1</b>&nbsp;&nbsp;"
                             "<span style='color:%2;'>%3: %4</span>&nbsp;&nbsp;"
                             "<span style='color:%5;'>%6: %7</span>&nbsp;&nbsp;"
                             "<span style='color:%8;'>%9: %10</span>")
                         .arg(tr_sv("验证完成"))
                         .arg(errs > 0 ? "#ff5252" : "#81c784")
                         .arg(tr_sv("错误"))
                         .arg(errs)
                         .arg(warns > 0 ? "#ffb74d" : "#81c784")
                         .arg(tr_sv("警告"))
                         .arg(warns)
                         .arg("#81c784")
                         .arg(tr_sv("通过"))
                         .arg(passes));
    v->addWidget(summary);

    auto* text = new QTextEdit(&dlg);
    text->setReadOnly(true);
    QString html = QStringLiteral(
        "<html><body style='color:#d7dbe2; font-family:monospace;'>");
    for (const auto& iss : result.issues) {
        QString color, tag;
        switch (iss.severity) {
            case Severity::Error:   color = "#ff5252"; tag = tr_sv("[错误]"); break;
            case Severity::Warning: color = "#ffb74d"; tag = tr_sv("[警告]"); break;
            default:                color = "#81c784"; tag = tr_sv("[通过]"); break;
        }
        html += QString("<p style='margin:2px 0;'><span style='color:%1;'>"
                        "<b>%2</b> %3</span></p>")
                    .arg(color, tag, iss.message.toHtmlEscaped());
    }
    html += QStringLiteral("</body></html>");
    text->setHtml(html);
    v->addWidget(text, 1);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    v->addWidget(btns);

    dlg.exec();
}

}} // namespace vsgraysim::gui
