/// @file EnvLabWindow.cpp —— rocky_env 三模块验证台实现。
#include "EnvLabWindow.h"

#include "Viewport3DView.h"
#include "ViewportConfig.h"
#include "ViewportContext.h"
#include "EnvFeatures.h"

#include <rocky_env/EnvApi.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>

#include <cmath>

namespace vsgraysim { namespace viewport3d {

namespace {

/// 一行 "标签 + 控件" 的紧凑布局辅助。
QWidget* row(const QString& label, QWidget* w, QWidget* extra = nullptr)
{
    auto* box = new QWidget;
    auto* lay = new QHBoxLayout(box);
    lay->setContentsMargins(0, 2, 0, 2);
    lay->setSpacing(6);
    auto* lb = new QLabel(label);
    lb->setMinimumWidth(96);
    lay->addWidget(lb);
    lay->addWidget(w, 1);
    if (extra) lay->addWidget(extra);
    return box;
}

QSlider* makeSlider(int lo, int hi, int val)
{
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(lo, hi);
    s->setValue(val);
    return s;
}

const char* layerTypeName(int t)
{
    switch (t) {
        case 0: return "卷云 Cirrus";
        case 1: return "层云 Stratus";
        case 2: return "层积云 Stratocumulus";
        case 3: return "积云 Cumulus";
        case 4: return "积雨云 Cumulonimbus";
        default: return "?";
    }
}

/// 默认四层配置: 高空卷云 + 层积云 + 低空积云 + 层云, 展示多层语义。
std::vector<rockyenv::CloudLayerParams> defaultLayers()
{
    std::vector<rockyenv::CloudLayerParams> v;
    rockyenv::CloudLayerParams a;
    a.type = rockyenv::CloudLayerType::Cirrus;
    a.altitude = 6500.0f; a.thickness = 900.0f;
    a.coverage = 0.35f;   a.density = 0.25f;
    v.push_back(a);

    rockyenv::CloudLayerParams b;
    b.type = rockyenv::CloudLayerType::Stratocumulus;
    b.altitude = 1800.0f; b.thickness = 500.0f;
    b.coverage = 0.45f;   b.density = 0.5f;
    v.push_back(b);

    rockyenv::CloudLayerParams c;
    c.type = rockyenv::CloudLayerType::Cumulus;
    c.altitude = 900.0f;  c.thickness = 700.0f;
    c.coverage = 0.40f;   c.density = 0.6f;
    v.push_back(c);

    rockyenv::CloudLayerParams d;
    d.type = rockyenv::CloudLayerType::Stratus;
    d.altitude = 420.0f;  d.thickness = 300.0f;
    d.coverage = 0.30f;   d.density = 0.4f;
    v.push_back(d);
    return v;
}

}  // namespace

EnvLabWindow::EnvLabWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("rocky_env 环境模块验证台 (sky / cloud / ocean)"));
    resize(1600, 900);
    buildUi();

    // 视口就绪后挂载默认模块组合。
    connect(view_, &Viewport3DView::vsgReady, this, [this]() {
        log(QStringLiteral("VSG 视口就绪, 开始挂载环境模块"));
        syncModules();
    });
    // 帧节拍 → 刷新读数 (节流, 避免拖慢渲染)。
    connect(view_, &Viewport3DView::frameTick, this, [this](const FrameTick&) {
        static uint64_t counter = 0;
        if (++counter % 15 != 0) return;
        refreshReadouts();
    });
    connect(view_, &Viewport3DView::fpsUpdated, this, [this](double fps) {
        if (fps_label_)
            fps_label_->setText(QStringLiteral("FPS: %1").arg(fps, 0, 'f', 1));
    });

    view_->setAutoFrame(true, 60);
}

EnvLabWindow::~EnvLabWindow() = default;

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
void EnvLabWindow::buildUi()
{
    view_ = new Viewport3DView(this);
    // Rocky 地球基座预设: rocky_map 建立锚点/ECEF 世界根, 三模块在其上验证。
    view_->applyConfig(ViewportConfig::fromPreset("env_lab"));

    auto* right = new QWidget;
    auto* right_lay = new QVBoxLayout(right);
    right_lay->setContentsMargins(8, 8, 8, 8);
    right_lay->setSpacing(8);

    // ---- 模块开关 ----
    auto* mod_box = new QGroupBox(QStringLiteral("模块加载 (分别 / 组合)"));
    auto* mod_lay = new QHBoxLayout(mod_box);
    cb_sky_   = new QCheckBox(QStringLiteral("rocky_sky"));
    cb_cloud_ = new QCheckBox(QStringLiteral("rocky_cloud"));
    cb_ocean_ = new QCheckBox(QStringLiteral("rocky_ocean"));
    cb_sky_->setChecked(true);
    cb_cloud_->setChecked(true);
    cb_ocean_->setChecked(true);
    mod_lay->addWidget(cb_sky_);
    mod_lay->addWidget(cb_cloud_);
    mod_lay->addWidget(cb_ocean_);
    right_lay->addWidget(mod_box);

    for (auto* cb : {cb_sky_, cb_cloud_, cb_ocean_})
        connect(cb, &QCheckBox::toggled, this, [this]() { syncModules(); });

    right_lay->addWidget(buildSkyPanel());
    right_lay->addWidget(buildCloudPanel());
    right_lay->addWidget(buildOceanPanel());
    right_lay->addWidget(buildStatusPanel(), 1);

    // ---- 日志 ----
    auto* log_box = new QGroupBox(QStringLiteral("运行日志"));
    auto* log_lay = new QVBoxLayout(log_box);
    log_view_ = new QPlainTextEdit;
    log_view_->setReadOnly(true);
    log_view_->setMaximumBlockCount(500);
    log_view_->setMinimumHeight(120);
    log_lay->addWidget(log_view_);
    right_lay->addWidget(log_box);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(view_);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    setCentralWidget(split);
}

QWidget* EnvLabWindow::buildSkyPanel()
{
    auto* box = new QGroupBox(QStringLiteral("rocky_sky —— 大气/星历"));
    auto* lay = new QVBoxLayout(box);

    sky_hours_ = new QDoubleSpinBox;
    sky_hours_->setRange(0.0, 24.0);
    sky_hours_->setSingleStep(0.5);
    sky_hours_->setDecimals(2);
    sky_hours_->setValue(4.0);
    sky_hours_->setSuffix(QStringLiteral(" h UTC"));
    connect(sky_hours_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { pushSkyParams(); });
    lay->addWidget(row(QStringLiteral("时间基准"), sky_hours_));

    sky_time_scale_ = new QDoubleSpinBox;
    sky_time_scale_->setRange(0.0, 3600.0);
    sky_time_scale_->setValue(1.0);
    sky_time_scale_->setSuffix(QStringLiteral(" ×"));
    connect(sky_time_scale_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { pushSkyParams(); });
    lay->addWidget(row(QStringLiteral("时间倍率"), sky_time_scale_));

    sky_exposure_ = makeSlider(10, 400, 100);
    connect(sky_exposure_, &QSlider::valueChanged, this, [this](int) { pushSkyParams(); });
    lay->addWidget(row(QStringLiteral("曝光"), sky_exposure_));

    sky_turbidity_ = makeSlider(0, 400, 100);
    connect(sky_turbidity_, &QSlider::valueChanged, this, [this](int) { pushSkyParams(); });
    lay->addWidget(row(QStringLiteral("浊度"), sky_turbidity_));

    sky_show_sun_ = new QCheckBox(QStringLiteral("太阳"));
    sky_show_sun_->setChecked(true);
    sky_show_moon_ = new QCheckBox(QStringLiteral("月亮"));
    sky_show_moon_->setChecked(true);
    sky_show_atm_ = new QCheckBox(QStringLiteral("大气层"));
    sky_show_atm_->setChecked(true);
    for (auto* cb : {sky_show_sun_, sky_show_moon_, sky_show_atm_})
        connect(cb, &QCheckBox::toggled, this, [this]() { pushSkyParams(); });
    auto* toggles = new QWidget;
    auto* tl = new QHBoxLayout(toggles);
    tl->setContentsMargins(0, 2, 0, 2);
    tl->addWidget(sky_show_sun_);
    tl->addWidget(sky_show_moon_);
    tl->addWidget(sky_show_atm_);
    lay->addWidget(row(QStringLiteral("天体开关"), toggles));

    sky_readout_ = new QLabel(QStringLiteral("—"));
    sky_readout_->setWordWrap(true);
    lay->addWidget(sky_readout_);
    return box;
}

QWidget* EnvLabWindow::buildCloudPanel()
{
    auto* box = new QGroupBox(QStringLiteral("rocky_cloud —— 多层云"));
    auto* lay = new QVBoxLayout(box);

    cloud_cover_ = makeSlider(0, 100, 50);
    connect(cloud_cover_, &QSlider::valueChanged, this, [this](int) { pushCloudParams(); });
    lay->addWidget(row(QStringLiteral("全局云量"), cloud_cover_));

    cloud_layer_count_ = new QSpinBox;
    cloud_layer_count_->setRange(0, 4);
    cloud_layer_count_->setValue(3);   // 默认三层语义化
    cloud_layer_count_->setToolTip(QStringLiteral("0 = 一期单层兼容模式"));
    connect(cloud_layer_count_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { pushCloudParams(); });
    lay->addWidget(row(QStringLiteral("层数"), cloud_layer_count_));

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
    const auto defaults = defaultLayers();
    for (int i = 0; i < 4; ++i) {
        cloud_layer_type_[i] = new QComboBox;
        for (int t = 0; t < 5; ++t)
            cloud_layer_type_[i]->addItem(QString::fromUtf8(layerTypeName(t)));
        cloud_layer_type_[i]->setCurrentIndex(
            static_cast<int>(defaults[static_cast<size_t>(i)].type));

        cloud_layer_alt_[i] = new QDoubleSpinBox;
        cloud_layer_alt_[i]->setRange(100.0, 14000.0);
        cloud_layer_alt_[i]->setSingleStep(100.0);
        cloud_layer_alt_[i]->setValue(defaults[static_cast<size_t>(i)].altitude);
        cloud_layer_alt_[i]->setSuffix(QStringLiteral(" m"));

        cloud_layer_cover_[i] = makeSlider(0, 100,
            static_cast<int>(defaults[static_cast<size_t>(i)].coverage * 100.0f));

        cloud_layer_on_[i] = new QCheckBox;
        cloud_layer_on_[i]->setChecked(i < 3);

        grid->addWidget(new QLabel(QStringLiteral("层%1").arg(i + 1)), i, 0);
        grid->addWidget(cloud_layer_on_[i], i, 1);
        grid->addWidget(cloud_layer_type_[i], i, 2);
        grid->addWidget(cloud_layer_alt_[i], i, 3);
        grid->addWidget(cloud_layer_cover_[i], i, 4);

        connect(cloud_layer_type_[i], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { pushCloudParams(); });
        connect(cloud_layer_alt_[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double) { pushCloudParams(); });
        connect(cloud_layer_cover_[i], &QSlider::valueChanged,
                this, [this](int) { pushCloudParams(); });
        connect(cloud_layer_on_[i], &QCheckBox::toggled,
                this, [this](bool) { pushCloudParams(); });
    }
    lay->addLayout(grid);

    cloud_readout_ = new QLabel(QStringLiteral("—"));
    cloud_readout_->setWordWrap(true);
    lay->addWidget(cloud_readout_);
    return box;
}

QWidget* EnvLabWindow::buildOceanPanel()
{
    auto* box = new QGroupBox(QStringLiteral("rocky_ocean —— 波场/海况"));
    auto* lay = new QVBoxLayout(box);

    ocean_wind_ = makeSlider(0, 300, 100);   // 0.1 m/s 精度 → 0..30 m/s
    connect(ocean_wind_, &QSlider::valueChanged, this, [this](int) { pushOceanParams(); });
    lay->addWidget(row(QStringLiteral("风速 (m/s)"), ocean_wind_));

    ocean_wind_az_ = new QDoubleSpinBox;
    ocean_wind_az_->setRange(0.0, 360.0);
    ocean_wind_az_->setValue(80.0);
    ocean_wind_az_->setSuffix(QStringLiteral(" °"));
    connect(ocean_wind_az_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { pushOceanParams(); });
    lay->addWidget(row(QStringLiteral("风向"), ocean_wind_az_));

    ocean_auto_hs_ = new QCheckBox(QStringLiteral("自动海况 (风→JONSWAP)"));
    ocean_auto_hs_->setChecked(true);
    connect(ocean_auto_hs_, &QCheckBox::toggled, this, [this](bool) { pushOceanParams(); });
    lay->addWidget(ocean_auto_hs_);

    ocean_wave_hs_ = makeSlider(0, 800, 130);   // 0..8 m, 0.01 m 精度
    ocean_wave_hs_->setToolTip(QStringLiteral("自动海况开启时忽略"));
    connect(ocean_wave_hs_, &QSlider::valueChanged, this, [this](int) { pushOceanParams(); });
    lay->addWidget(row(QStringLiteral("有效波高 Hs"), ocean_wave_hs_));

    ocean_harmonics_ = new QSpinBox;
    ocean_harmonics_->setRange(4, 16);
    ocean_harmonics_->setValue(8);
    ocean_harmonics_->setToolTip(QStringLiteral("Gerstner 谐波数 (4..16)"));
    connect(ocean_harmonics_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { pushOceanParams(); });
    lay->addWidget(row(QStringLiteral("谐波数"), ocean_harmonics_));

    ocean_swell_h_ = new QDoubleSpinBox;
    ocean_swell_h_->setRange(0.0, 8.0);
    ocean_swell_h_->setSingleStep(0.1);
    ocean_swell_h_->setValue(0.0);
    ocean_swell_h_->setSuffix(QStringLiteral(" m"));
    connect(ocean_swell_h_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { pushOceanParams(); });
    lay->addWidget(row(QStringLiteral("涌浪 Hs"), ocean_swell_h_));

    ocean_swell_len_ = new QDoubleSpinBox;
    ocean_swell_len_->setRange(50.0, 600.0);
    ocean_swell_len_->setValue(180.0);
    ocean_swell_len_->setSuffix(QStringLiteral(" m"));
    connect(ocean_swell_len_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { pushOceanParams(); });
    lay->addWidget(row(QStringLiteral("涌浪波长"), ocean_swell_len_));

    ocean_swell_dir_ = new QDoubleSpinBox;
    ocean_swell_dir_->setRange(-180.0, 180.0);
    ocean_swell_dir_->setValue(35.0);
    ocean_swell_dir_->setSuffix(QStringLiteral(" °"));
    connect(ocean_swell_dir_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { pushOceanParams(); });
    lay->addWidget(row(QStringLiteral("涌浪偏角"), ocean_swell_dir_));

    ocean_reflect_ = new QComboBox;
    ocean_reflect_->addItems({QStringLiteral("0 = 梯度"),
                              QStringLiteral("1 = 探针级天空反射")});
    ocean_reflect_->setCurrentIndex(1);
    connect(ocean_reflect_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { pushOceanParams(); });
    lay->addWidget(row(QStringLiteral("反射档位"), ocean_reflect_));

    ocean_curvature_ = new QCheckBox(QStringLiteral("球面曲率 (>20km 海域)"));
    connect(ocean_curvature_, &QCheckBox::toggled, this, [this](bool) { pushOceanParams(); });
    lay->addWidget(ocean_curvature_);

    ocean_readout_ = new QLabel(QStringLiteral("—"));
    ocean_readout_->setWordWrap(true);
    lay->addWidget(ocean_readout_);
    return box;
}

QWidget* EnvLabWindow::buildStatusPanel()
{
    auto* box = new QGroupBox(QStringLiteral("状态 / 采样"));
    auto* lay = new QVBoxLayout(box);

    fps_label_ = new QLabel(QStringLiteral("FPS: —"));
    lay->addWidget(fps_label_);

    auto* probe = new QLabel(
        QStringLiteral("IOceanSampler / ICloudQuery 每帧回读;\n"
                       "云影/海况与渲染画面同源 (CPU-GPU 同公式)。"));
    probe->setWordWrap(true);
    probe->setStyleSheet(QStringLiteral("color:#888;"));
    lay->addWidget(probe);
    return box;
}

// ---------------------------------------------------------------------------
// 模块挂载 / 卸载 —— "分别加载三个模块" 的实现
// ---------------------------------------------------------------------------
bool EnvLabWindow::syncModules()
{
    if (!view_ || !view_->isReady()) return false;

    bool changed = false;

    // ---- sky ----
    {
        const bool want = cb_sky_->isChecked();
        const bool have = (sky_ != nullptr);
        if (want != have) {
            if (want) {
                nlohmann::json p;
                p["hours_utc"]   = sky_hours_->value();
                p["time_scale"]  = sky_time_scale_->value();
                p["show_atmosphere"] = sky_show_atm_->isChecked();
                p["show_sun"]    = sky_show_sun_->isChecked();
                p["show_moon"]   = sky_show_moon_->isChecked();
                p["exposure"]    = sky_exposure_->value() / 100.0f;
                p["turbidity"]   = sky_turbidity_->value() / 100.0f;
                view_->addFeature(SkyFeature::kId, p);
                log(QStringLiteral("[sky] 模块已加载"));
            } else {
                view_->removeFeature(SkyFeature::kId);
                log(QStringLiteral("[sky] 模块已卸载"));
            }
            changed = true;
        }
    }

    // ---- cloud ----
    {
        const bool want = cb_cloud_->isChecked();
        const bool have = (cloud_ != nullptr);
        if (want != have) {
            if (want) {
                nlohmann::json p;
                p["cover"] = cloud_cover_->value() / 100.0f;
                view_->addFeature(CloudFeature::kId, p);
                log(QStringLiteral("[cloud] 模块已加载"));
            } else {
                view_->removeFeature(CloudFeature::kId);
                log(QStringLiteral("[cloud] 模块已卸载"));
            }
            changed = true;
        }
    }

    // ---- ocean ----
    {
        const bool want = cb_ocean_->isChecked();
        const bool have = (ocean_ != nullptr);
        if (want != have) {
            if (want) {
                nlohmann::json p;
                p["wind_speed"]  = ocean_wind_->value() / 10.0f;
                p["wind_az"]     = ocean_wind_az_->value();
                p["wave_height"] = ocean_wave_hs_->value() / 100.0f;
                p["ocean_size"]  = 4000.0;
                view_->addFeature(OceanFeature::kId, p);
                log(QStringLiteral("[ocean] 模块已加载"));
            } else {
                view_->removeFeature(OceanFeature::kId);
                log(QStringLiteral("[ocean] 模块已卸载"));
            }
            changed = true;
        }
    }

    if (changed) {
        refreshFeaturePointers();
        // 新挂载的模块立即吃一次面板参数 (含默认分层/涌浪等)。
        pushSkyParams();
        pushCloudParams();
        pushOceanParams();
    }
    return changed;
}

void EnvLabWindow::refreshFeaturePointers()
{
    if (!view_) return;
    sky_   = view_->featureAs<SkyFeature>(SkyFeature::kId);
    cloud_ = view_->featureAs<CloudFeature>(CloudFeature::kId);
    ocean_ = view_->featureAs<OceanFeature>(OceanFeature::kId);
}

// ---------------------------------------------------------------------------
// 面板 → 模块
// ---------------------------------------------------------------------------
void EnvLabWindow::pushSkyParams()
{
    if (pushing_) return;
    if (!sky_) return;

    // 运行时可控项走 ISkyControl; 时间基准/倍率需经 configure 重建时钟基准。
    if (auto* c = sky_->skyControl()) {
        c->setExposure(static_cast<float>(sky_exposure_->value() / 100.0));
        c->setTurbidity(static_cast<float>(sky_turbidity_->value() / 100.0));
        c->setShowSun(sky_show_sun_->isChecked());
        c->setShowMoon(sky_show_moon_->isChecked());
        c->setShowAtmosphere(sky_show_atm_->isChecked());
    }
    // 时间基准/倍率: 通过 configure 下发 (SkyFeature 会重置仿真时钟基准)。
    nlohmann::json p;
    p["hours_utc"]       = sky_hours_->value();
    p["time_scale"]      = sky_time_scale_->value();
    p["show_atmosphere"] = sky_show_atm_->isChecked();
    view_->configureFeature(SkyFeature::kId, p);
}

void EnvLabWindow::pushCloudParams()
{
    if (pushing_) return;
    if (!cloud_) return;
    auto* c = cloud_->cloudControl();
    if (!c) return;

    c->setCoverage(static_cast<float>(cloud_cover_->value() / 100.0));

    const int n = cloud_layer_count_->value();
    if (n <= 0) {
        // 单层兼容模式: 空 layers。
        c->setLayers({});
        return;
    }

    std::vector<rockyenv::CloudLayerParams> layers;
    const auto defaults = defaultLayers();
    layers.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        rockyenv::CloudLayerParams lp = defaults[static_cast<size_t>(i)];
        lp.type      = static_cast<rockyenv::CloudLayerType>(
                           cloud_layer_type_[i]->currentIndex());
        lp.altitude  = static_cast<float>(cloud_layer_alt_[i]->value());
        lp.coverage  = static_cast<float>(cloud_layer_cover_[i]->value() / 100.0);
        layers.push_back(lp);
    }
    c->setLayers(layers);

    // 逐层开关 (index 与 layers 对齐)。
    for (int i = 0; i < n; ++i)
        c->setLayerEnabled(i, cloud_layer_on_[i]->isChecked());
}

void EnvLabWindow::pushOceanParams()
{
    if (pushing_) return;
    if (!ocean_) return;
    auto* c = ocean_->oceanControl();
    if (!c) return;

    const float wind = static_cast<float>(ocean_wind_->value() / 10.0);
    const float az   = static_cast<float>(ocean_wind_az_->value());
    const float azr  = static_cast<float>(az * M_PI / 180.0);
    // 风向: 方位角 (北=0 顺时针) → ENU (x=东, z=南)。
    const vsg::vec3 dir(std::sin(azr), 0.0f, -std::cos(azr));
    c->setWind(wind, dir);

    if (ocean_auto_hs_->isChecked()) {
        c->setWaveHeight(0.0f);   // 0 = 恢复气象驱动 (风→JONSWAP)
    } else {
        c->setWaveHeight(static_cast<float>(ocean_wave_hs_->value() / 100.0));
    }

    c->setSwell(static_cast<float>(ocean_swell_h_->value()),
                static_cast<float>(ocean_swell_len_->value()),
                static_cast<float>(ocean_swell_dir_->value()));
    c->setReflection(ocean_reflect_->currentIndex());
    c->setCurvature(ocean_curvature_->isChecked());
}

// ---------------------------------------------------------------------------
// 模块 → 面板 (每帧回读, 验证 Query 接口)
// ---------------------------------------------------------------------------
void EnvLabWindow::refreshReadouts()
{
    if (pushing_) return;

    // ---- sky: ISkyControl 回显 + 星历太阳 ----
    if (sky_ && sky_readout_) {
        QString s;
        if (auto* c = sky_->skyControl()) {
            s += QStringLiteral("曝光 %1  浊度 %2\n")
                     .arg(c->getExposure(), 0, 'f', 2)
                     .arg(c->getTurbidity(), 0, 'f', 2);
        }
        if (auto* sp = sky_->sunProvider()) {
            rockyenv::EnvSunInfo si;
            const vsg::dmat4 model = view_->context().contentModel();
            if (sp->sunInfo(model, si)) {
                s += QStringLiteral("太阳 高度 %1°  方位 %2°\n")
                         .arg(si.elevationDeg, 0, 'f', 1)
                         .arg(si.azimuthDeg, 0, 'f', 1);
            }
        }
        if (auto* lp = sky_->lightingProvider()) {
            rockyenv::AtmosphereState st;
            if (lp->getAtmosphereState(st)) {
                s += QStringLiteral("昼 %1  直射(%2,%3,%4)  天光(%5,%6,%7)")
                         .arg(st.is_day ? QStringLiteral("是") : QStringLiteral("否"))
                         .arg(static_cast<double>(st.sun_light.x), 0, 'f', 2)
                         .arg(static_cast<double>(st.sun_light.y), 0, 'f', 2)
                         .arg(static_cast<double>(st.sun_light.z), 0, 'f', 2)
                         .arg(static_cast<double>(st.ambient_light.x), 0, 'f', 2)
                         .arg(static_cast<double>(st.ambient_light.y), 0, 'f', 2)
                         .arg(static_cast<double>(st.ambient_light.z), 0, 'f', 2);
            }
        }
        sky_readout_->setText(s.isEmpty() ? QStringLiteral("(未就绪)") : s);
    }

    // ---- cloud: ICloudQuery 云底/云顶/云量/云影 ----
    if (cloud_ && cloud_readout_) {
        QString s;
        if (auto* q = cloud_->cloudQuery()) {
            float base = 0.0f, top = 0.0f;
            const bool hasBase = q->cloudBase(base);
            const bool hasTop  = q->cloudTop(top);
            s += QStringLiteral("云量 %1\n").arg(q->totalCoverage(), 0, 'f', 2);
            s += hasBase ? QStringLiteral("云底 %1 m\n").arg(base, 0, 'f', 0)
                         : QStringLiteral("云底 —\n");
            s += hasTop  ? QStringLiteral("云顶 %1 m\n").arg(top, 0, 'f', 0)
                         : QStringLiteral("云顶 —\n");
            // 云影因子: 锚点 ENU 原点处, 用当前太阳高度角。
            float sunEl = 60.0f;
            if (auto* sp = sky_->sunProvider()) {
                rockyenv::EnvSunInfo si;
                if (sp->sunInfo(view_->context().contentModel(), si))
                    sunEl = si.elevationDeg;
            }
            const float shadow = q->shadowFactor(
                0.0, 0.0, static_cast<double>(sunEl * static_cast<float>(M_PI / 180.0)));
            s += QStringLiteral("云影因子 %1 (1=无云满光照)").arg(shadow, 0, 'f', 3);
        }
        cloud_readout_->setText(s.isEmpty() ? QStringLiteral("(未就绪)") : s);
    }

    // ---- ocean: IOceanSampler 海况 + IOceanControl 回显 ----
    if (ocean_ && ocean_readout_) {
        QString s;
        if (auto* smp = ocean_->oceanSampler()) {
            const rockyenv::SeaState st = smp->seaState();
            s += QStringLiteral("Hs %1 m  Tp %2 s  蒲福 %3 级\n")
                     .arg(st.significant_wave_height, 0, 'f', 2)
                     .arg(st.peak_period, 0, 'f', 2)
                     .arg(st.beaufort);
            // 锚点 ENU 原点采样 (平面模式下即世界原点): 波高/法线/轨道速度/白沫。
            const double ex = 0.0, ez = 0.0, t = 0.0;
            const float h  = smp->waveHeight(ex, ez, t);
            const vsg::vec3 nrm = smp->waveNormal(ex, ez, t);
            const vsg::vec3 vel = smp->orbitalVelocity(ex, ez, t);
            const float foam = smp->foamCoverage(ex, ez, t);
            s += QStringLiteral("静态诊断采样(原点，t=0 s) 波高 %1 m\n").arg(h, 0, 'f', 3);
            s += QStringLiteral("法线 (%1, %2, %3)\n")
                     .arg(static_cast<double>(nrm.x), 0, 'f', 3)
                     .arg(static_cast<double>(nrm.y), 0, 'f', 3)
                     .arg(static_cast<double>(nrm.z), 0, 'f', 3);
            s += QStringLiteral("轨道速度 %1 m/s  白沫 %2")
                     .arg(static_cast<double>(vsg::length(vel)), 0, 'f', 3)
                     .arg(static_cast<double>(foam), 0, 'f', 3);
        }
        if (auto* c = ocean_->oceanControl()) {
            s += QStringLiteral("\n控制: 反射 %1  曲率 %2")
                     .arg(c->getReflection())
                     .arg(c->getCurvature() ? QStringLiteral("开") : QStringLiteral("关"));
        }
        ocean_readout_->setText(s.isEmpty() ? QStringLiteral("(未就绪)") : s);
    }
}

void EnvLabWindow::log(const QString& msg)
{
    if (log_view_)
        log_view_->appendPlainText(msg);
    spdlog::info("[env_lab] {}", msg.toStdString());
}

}} // namespace vsgraysim::viewport3d
