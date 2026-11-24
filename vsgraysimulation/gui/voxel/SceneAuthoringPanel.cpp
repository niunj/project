#include "SceneAuthoringPanel.h"
#include <vsgraysim/earth/VoxelOcean.h>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDateEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace vsgraysim::ui {
using json = nlohmann::json;
namespace {
QDoubleSpinBox *number(QFormLayout *f, const QString &name, double initial, double low = -100000,
                       double high = 100000, int decimals = 3) {
    auto *s = new QDoubleSpinBox;
    s->setRange(low, high);
    s->setDecimals(decimals);
    s->setValue(initial);
    f->addRow(name, s);
    return s;
}
bool accept(QDialog &d, QFormLayout *f) {
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    f->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    return d.exec() == QDialog::Accepted;
}
} // namespace
SceneAuthoringPanel::SceneAuthoringPanel(std::function<earth::VoxelScene *()> world,
                                         std::function<void()> changed, QWidget *parent)
    : QWidget(parent), world_(std::move(world)), changed_(std::move(changed)) {
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel(tr("正式体素专题编辑\n局部坐标：X 东 / Y 上 / Z "
                               "南，单位米。\n人工赋值；显示细分不增加源数据精度。"));
    note->setWordWrap(true);
    layout->addWidget(note);
    objects_ = new QComboBox;
    layout->addWidget(objects_);
    auto button = [&](const QString &text, auto action) {
        auto *b = new QPushButton(text);
        layout->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, action] { run(action); });
    };
    button(tr("取消后台导入"), [this] {
        if (import_)
            import_->cancellation->store(true);
    });
    button(tr("导入目标模型…"), [this] { import(false); });
    button(tr("导入影像 / 高程工作区…"), [this] { import(true); });
    button(tr("地形抬升 / 降低 / 平整 / 平滑…"), [this] { sculpt(); });
    button(tr("光学 / 红外 / 雷达属性…"), [this] { material(); });
    button(tr("时间 / 天气 / 雨雪…"), [this] { environment(); });
    status_ = new QLabel;
    status_->setWordWrap(true);
    layout->addWidget(status_);
    layout->addStretch();
    auto *timer = new QTimer(this);
    timer->setInterval(300);
    connect(timer, &QTimer::timeout, this, [this] { refresh(); });
    timer->start();
}
earth::VoxelScene &SceneAuthoringPanel::world() {
    auto *w = world_();
    if (!w)
        throw std::runtime_error("请先打开并等待体素专题加载完成");
    if (!w->paused())
        throw std::runtime_error("请先暂停仿真再编辑");
    return *w;
}
std::string SceneAuthoringPanel::selected() {
    if (objects_->currentText().isEmpty())
        throw std::runtime_error("请选择体素对象");
    return objects_->currentText().toStdString();
}
void SceneAuthoringPanel::environment() {
    auto& w=world();const auto snapshot=w.snapshot();
    QDialog dialog(this);dialog.setWindowTitle(tr("场景时间与区域天气"));dialog.resize(620,720);
    auto* form=new QFormLayout(&dialog);
    auto* date=new QDateEdit(QDate(snapshot->utcOrigin.at("year"),snapshot->utcOrigin.at("month"),snapshot->utcOrigin.at("day")));
    date->setCalendarPopup(true);date->setDateRange(QDate(1900,1,1),QDate(2200,12,31));form->addRow(tr("仿真零时刻日期（UTC）"),date);
    auto* hour=number(form,tr("仿真零时刻小时（UTC）"),snapshot->utcOrigin.at("hours_utc"),0,23.99999,5);
    auto* rate=number(form,tr("昼夜倍率（不改变动力学步长）"),snapshot->utcOrigin.value("time_scale",1.),0,3600,2);
    auto* utcPreview=new QLabel;
    form->addRow(tr("当前场景时钟"),utcPreview);
    const auto origin=[=]{const auto d=date->date();return json{{"year",d.year()},{"month",d.month()},{"day",d.day()},
        {"hours_utc",hour->value()},{"time_scale",rate->value()}};};
    const auto refreshClock=[=]{
        const auto utc=earth::sceneUtc(origin(),snapshot->timeSeconds());
        const auto seconds=std::clamp(int(utc.hours*3600.),0,86399);
        utcPreview->setText(tr("%1 %2 UTC").arg(QDate(utc.year,utc.month,utc.day).toString(Qt::ISODate),
            QTime(0,0).addSecs(seconds).toString(QStringLiteral("HH:mm:ss"))));
    };
    connect(date,&QDateEdit::dateChanged,&dialog,refreshClock);
    connect(hour,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,refreshClock);
    connect(rate,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,refreshClock);
    refreshClock();
    auto* enabled=new QCheckBox(tr("启用场景统一天气"));enabled->setChecked(snapshot->weather.enabled);form->addRow(enabled);
    auto* presets=new QComboBox;
    presets->addItem(tr("选择天气预设"));
    for(const auto& p:std::vector<std::pair<const char*,const char*>>{{"晴朗","clear"},{"晴天积云","fair_clouds"},
        {"多云","cloudy"},{"阴天","overcast"},{"厚乌云","storm_clouds"},{"晨昏云","twilight_clouds"},
        {"小雨","light_rain"},{"中雨","moderate_rain"},{"大雨","heavy_rain"},
        {"小雪","light_snow"},{"中雪","moderate_snow"},{"大雪","heavy_snow"},{"雾","fog"}})
        presets->addItem(QString::fromUtf8(p.first),p.second);
    form->addRow(tr("预设（选择后填充初始状态）"),presets);
    auto* initial=new QPlainTextEdit(QString::fromStdString(earth::SceneWeather::encodeState(snapshot->weather.initial).dump(2)));
    form->addRow(tr("初始气象参数"),initial);
    auto* transitions=new QPlainTextEdit(QString::fromStdString(snapshot->weather.encode().at("transitions").dump(2)));
    form->addRow(tr("天气转场时间线（仿真秒）"),transitions);
    auto* sea=new QComboBox;
    if(snapshot->ocean){
        sea->addItem(tr("沿用场景海况"),-1);
        for(int force=0;force<=8;++force){
            const auto setting=earth::seaWindPreset(force);
            sea->addItem(tr("蒲福 %1 级 · %2 m/s · 风浪参数 %3 m")
                .arg(force).arg(setting.windSpeed).arg(setting.waveHeight),force);
        }
        form->addRow(tr("设置基础海况与初始风速"),sea);
        sea->setToolTip(tr("使用现有海洋波谱更新显示和浮力。方向取初始气象的 wind_azimuth_deg。后续天气转场仍按时间线改变风场。"));
    }else sea->setParent(&dialog);
    auto* note=new QLabel(tr("转场示例 [{\"at_seconds\":60,\"duration_seconds\":120,\"state\":{\"preset\":\"light_rain\"}}]\n"
        "rain_mm_h 为雨强，snow_water_mm_h 为雪水当量。晨昏云色由当前太阳高度决定。台北降雪用于人工气象测试。"));
    note->setWordWrap(true);form->addRow(note);
    connect(presets,qOverload<int>(&QComboBox::activated),&dialog,[=](int i){
        if(!presets->itemData(i).isValid())return;
        initial->setPlainText(QString::fromStdString(earth::SceneWeather::encodeState(earth::SceneWeather::preset(presets->itemData(i).toString().toStdString())).dump(2)));
        enabled->setChecked(true);
    });
    if(!accept(dialog,form))return;
    json weather={{"enabled",enabled->isChecked()},{"initial",json::parse(initial->toPlainText().toStdString())},{"transitions",json::parse(transitions->toPlainText().toStdString())}};
    auto commands=json::array();
    if(snapshot->ocean&&sea->currentData().toInt()>=0){
        const int force=sea->currentData().toInt();
        const auto settings=earth::SceneWeather::load(weather).initial;
        auto ocean=snapshot->ocean->encode();
        earth::applySeaWindPreset(ocean,force,enabled->isChecked()?settings.windAzimuth:snapshot->ocean->windAzimuth);
        weather["initial"]["wind_speed_mps"]=earth::seaWindPreset(force).windSpeed;
        commands.push_back({{"kind","ocean"},{"ocean",std::move(ocean)}});
    }
    commands.push_back({{"kind","environment"},{"utc_origin",origin()},{"weather",std::move(weather)}});
    w.edit(commands,snapshot->revision);
    changed_();status_->setText(tr("时间与天气已写入场景；保存专题或检查点后可重开。"));
}
void SceneAuthoringPanel::run(std::function<void()> action) {
    try {
        action();
    } catch (const std::exception &e) {
        status_->setText(QString::fromUtf8(e.what()));
        QMessageBox::warning(this, tr("体素场景编辑"), QString::fromUtf8(e.what()));
    }
}
void SceneAuthoringPanel::refresh() {
    if (import_ && import_->snapshot.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = std::move(import_->snapshot);
        const auto base = importBase_;
        import_.reset();
        importBase_.reset();
        run([&] {
            auto prepared = result.get();
            auto &w = world();
            if (w.snapshot() != base)
                throw std::runtime_error("导入期间场景已改变，请重新导入；原场景未修改");
            w.importPrepared(std::move(prepared), base->revision);
            status_->setText(tr("导入完成：占用、显示、碰撞与传感器统一提交"));
            changed_();
        });
    }
    auto *w = world_();
    QStringList ids;
    if (w)
        for (const auto &[id, o] : w->snapshot()->objects)
            ids << QString::fromStdString(id);
    QStringList current;
    for (int i = 0; i < objects_->count(); ++i)
        current << objects_->itemText(i);
    if (current != ids) {
        const auto chosen = objects_->currentText();
        objects_->clear();
        objects_->addItems(ids);
        if (ids.contains(chosen))
            objects_->setCurrentText(chosen);
    }
}
void SceneAuthoringPanel::sculpt() {
    auto &w = world();
    const auto snapshot = w.snapshot();
    const auto id = selected();
    const auto &o = snapshot->objects.at(id);
    if (!o.earth)
        throw std::runtime_error("请选择影像高程地形；该操作直接修改其高度与占用");
    const auto bounds = o.localBounds();
    QDialog d(this);
    d.setWindowTitle(tr("地形工作区编辑"));
    auto *f = new QFormLayout(&d);
    auto *mode = new QComboBox;
    mode->addItem(tr("抬升"), "raise");
    mode->addItem(tr("降低"), "lower");
    mode->addItem(tr("平整"), "flatten");
    mode->addItem(tr("平滑"), "smooth");
    f->addRow(tr("操作"), mode);
    auto *x = number(f, tr("中心 X（m）"), (bounds.min.x + bounds.max.x) * .5);
    auto *z = number(f, tr("中心 Z（m）"), (bounds.min.z + bounds.max.z) * .5);
    auto *radius = number(f, tr("半径（m）"), std::max(1., (bounds.max.x - bounds.min.x) * .15), .01, 100000);
    auto *amount = number(f, tr("升降幅度（m）"), 1, 0, 100000);
    auto *height = number(f, tr("平整高度 Y（m）"), bounds.max.y);
    auto *strength = number(f, tr("强度"), .5, .001, 1);
    auto *note = new QLabel(tr("仅修改已准备的地形工作区；超出固定垂直网格会拒绝操作。\n边缘平滑衰减；保留未"
                               "改变高度层中的既有破坏。"));
    note->setWordWrap(true);
    f->addRow(note);
    if (!accept(d, f))
        return;
    w.edit(json::array({{{"kind", "terrain_brush"},
                         {"object", id},
                         {"brush",
                          {{"operation", mode->currentData().toString().toStdString()},
                           {"center_m", {x->value(), 0, z->value()}},
                           {"radius_m", radius->value()},
                           {"amount_m", amount->value()},
                           {"height_m", height->value()},
                           {"strength", strength->value()}}}}}),
           snapshot->revision);
    status_->setText(tr("地形编辑已统一提交；请保存场景包"));
    changed_();
}
void SceneAuthoringPanel::material() {
    auto &w = world();
    const auto snapshot = w.snapshot();
    const auto id = selected();
    const auto &o = snapshot->objects.at(id);
    QDialog d(this);
    d.setWindowTitle(tr("体素光学、红外与雷达参数"));
    d.resize(550, 730);
    auto *layout = new QVBoxLayout(&d);
    auto *area = new QScrollArea;
    area->setWidgetResizable(true);
    auto *fields = new QWidget;
    auto *f = new QFormLayout(fields);
    area->setWidget(fields);
    layout->addWidget(area);
    auto *scope = new QComboBox;
    scope->addItem(tr("全部体素（清除旧分区覆盖）"), "whole");
    scope->addItem(tr("局部体素中心范围 AABB"), "region");
    f->addRow(tr("赋值范围"), scope);
    const auto &m = o.material;
    auto *temp = number(f, tr("温度（K）"), m.at("temperature_k"), 1, 5000);
    auto *rho = number(f, tr("可见光反射率"), m.at("rho_tv"), 0, 1);
    auto *swir = number(f, tr("短波红外反射率"), m.at("rho_swir"), 0, 1);
    auto *mwir = number(f, tr("中波红外发射率"), m.value("eps_mwir", m.at("emissivity").get<double>()), 0, 1);
    auto *lwir = number(f, tr("长波红外发射率"), m.value("eps_lwir", m.at("emissivity").get<double>()), 0, 1);
    const auto r = m.value("radar", json::object());
    auto *real = number(f, tr("介电常数实部"), r.value("dielectric_real", 6.), 1, 1e8);
    auto *imag = number(f, tr("介电常数虚部"), r.value("dielectric_imag", 0.), 0, 1e8);
    auto *model = new QSpinBox;
    model->setRange(0, 3);
    model->setValue(r.value("dielectric_model", 0));
    f->addRow(tr("介电模型编号"), model);
    auto *sigma = number(f, tr("后向散射系数 σ⁰（dB）"), r.value("sigma0_db", -20.), -120, 120);
    auto *rough = number(f, tr("归一化粗糙度"), r.value("surface_rough", .5), 0, 1);
    auto *scale = number(f, tr("RCS 比例（不是固定目标 RCS）"), r.value("rcs_scale", 1.), 0, 1e8);
    const auto b = o.localBounds();
    QDoubleSpinBox *low[3];
    QDoubleSpinBox *high[3];
    for (int a = 0; a < 3; ++a) {
        low[a] = number(f, tr("局部 %1 最小（m）").arg(QChar("XYZ"[a])), b.min[a]);
        high[a] = number(f, tr("局部 %1 最大（m）").arg(QChar("XYZ"[a])), b.max[a]);
    }
    auto *note = new QLabel(tr("参数为人工设定，不是实测标定。RCS "
                               "仍由形状、视角、频率与散射模型计算。多波段属性不等于 RGB 贴图颜色。"));
    note->setWordWrap(true);
    f->addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;
    json properties = {{"temperature_k", temp->value()},
                       {"rho_tv", rho->value()},
                       {"rho_swir", swir->value()},
                       {"emissivity", lwir->value()},
                       {"eps_mwir", mwir->value()},
                       {"eps_lwir", lwir->value()},
                       {"radar",
                        {{"dielectric_real", real->value()},
                         {"dielectric_imag", imag->value()},
                         {"dielectric_model", model->value()},
                         {"sigma0_db", sigma->value()},
                         {"surface_rough", rough->value()},
                         {"rcs_scale", scale->value()}}}};
    if (scope->currentData() == "whole") {
        auto merged = m;
        merged.update(properties);
        w.edit(json::array({{{"kind", "material"}, {"object", id}, {"material", merged}},
                            {{"kind", "sensor_materials"}, {"object", id}, {"sensor_materials", nullptr}}}),
               snapshot->revision);
    } else {
        json region = {{"min_m", {low[0]->value(), low[1]->value(), low[2]->value()}},
                       {"max_m", {high[0]->value(), high[1]->value(), high[2]->value()}}};
        w.edit(
            json::array(
                {{{"kind", "sensor_paint"}, {"object", id}, {"material", properties}, {"region", region}}}),
            snapshot->revision);
    }
    status_->setText(tr("多波段属性已提交；保存后由正式仿真装备读取"));
    changed_();
}
void SceneAuthoringPanel::import(bool terrain) {
    if (import_)
        throw std::runtime_error("已有导入任务，等待完成后再试");
    auto &w = world();
    const auto snapshot = w.snapshot();
    const auto file = QFileDialog::getOpenFileName(
        this, terrain ? tr("导入地形影像高程源") : tr("导入目标模型"), terrain ? "config/earth" : "",
        terrain ? tr("地图工程 (*.json *.ter *.vsgterrain)")
                : tr("目标模型 (*.vsg *.vsgt *.vsgb *.obj *.glb *.gltf *.fbx *.dae *.ply *.stl *.3ds)"));
    if (file.isEmpty())
        return;
    QDialog d(this);
    d.setWindowTitle(terrain ? tr("地形物理工作区") : tr("目标体素化"));
    auto *f = new QFormLayout(&d);
    auto *name =
        new QLineEdit(QString("%1-%2").arg(terrain ? "terrain" : "target").arg(snapshot->objects.size() + 1));
    f->addRow(tr("稳定对象 ID"), name);
    auto *cell = number(f, tr("体素边长（m；目标 0 为自动）"), terrain ? 30 : 0, terrain ? .01 : 0, 10000, 4);
    QDoubleSpinBox *minx = nullptr, *maxx = nullptr, *minz = nullptr, *maxz = nullptr, *bottom = nullptr,
                   *units = nullptr;
    QSpinBox *sub = nullptr, *detail = nullptr;
    QComboBox *up = nullptr;
    QDoubleSpinBox *position[3]{};
    if (terrain) {
        minx = number(f, tr("最小 X（m）"), -500);
        maxx = number(f, tr("最大 X（m）"), 500);
        minz = number(f, tr("最小 Z（m）"), -500);
        maxz = number(f, tr("最大 Z（m）"), 500);
        bottom = number(f, tr("体积底面 Y（m）"), -100);
        sub = new QSpinBox;
        sub->setRange(1, 8);
        sub->setValue(2);
        f->addRow(tr("每格显示细分（不增加真实数据）"), sub);
    } else {
        units = number(f, tr("源单位到米"), 1, 1e-9, 1e6, 9);
        up = new QComboBox;
        up->addItems({"y", "z"});
        if (file.endsWith(".ive") || file.endsWith(".osg") || file.endsWith(".osgb"))
            up->setCurrentText("z");
        f->addRow(tr("向上轴"), up);
        detail = new QSpinBox;
        detail->setRange(16, 512);
        detail->setValue(512);
        f->addRow(tr("最长轴格数上限（预算约束）"), detail);
        for (int a = 0; a < 3; ++a)
            position[a] = number(f, tr("位置 %1（m）").arg(QChar("XYZ"[a])), 0);
    }
    auto *note = new QLabel(tr("源数据与体素化在后台准备，完成后原子提交。\n当前仍使用有界精细物理工作区；超"
                               "预算会拒绝，不降低已有精度。"));
    note->setWordWrap(true);
    f->addRow(note);
    if (!accept(d, f))
        return;
    json source;
    json object = {{"id", name->text().toStdString()}, {"role", terrain ? "terrain" : "target"}};
    if (terrain) {
        if (minx->value() >= maxx->value() || minz->value() >= maxz->value())
            throw std::runtime_error("地形范围为空");
        source = {{"type", "earth"},
                  {"map", QFileInfo(file).absoluteFilePath().toStdString()},
                  {"cell_size_m", cell->value()},
                  {"bottom_m", bottom->value()},
                  {"hd_subdivisions", sub->value()},
                  {"mode", "voxel"},
                  {"earth",
                   {{"strict", true},
                    {"grid_size", std::clamp(int(std::ceil(std::max(maxx->value() - minx->value(),
                                                                    maxz->value() - minz->value()) /
                                                           cell->value() * sub->value())) +
                                                 1,
                                             2, 513)},
                    {"imagery_size", 512},
                    {"coverage",
                     {{"min_x", minx->value()},
                      {"max_x", maxx->value()},
                      {"min_z", minz->value()},
                      {"max_z", maxz->value()}}}}}};
    } else {
        source = {{"type", "mesh"},
                  {"path", QFileInfo(file).absoluteFilePath().toStdString()},
                  {"voxel_size_m", cell->value()},
                  {"max_dimension", detail->value()},
                  {"units_to_metres", units->value()},
                  {"up_axis", up->currentText().toStdString()},
                  {"fill_interior", true},
                  {"surface_detail", "source_clipped"}};
        object["position_m"] = {position[0]->value(), position[1]->value(), position[2]->value()};
        object["physics"] = {{"motion", "prescribed"}, {"fracture", true}};
    }
    object["source"] = source;
    const auto &a = snapshot->anchor;
    json document = {
        {"format", "voxel-scene-v1"},
        {"geo_anchor",
         {{"lat", a.lat_deg}, {"lon", a.lon_deg}, {"alt", a.alt_m}, {"heading_deg", a.heading_deg}}},
        {"loading", {{"workers", 1}, {"target_jobs", 1}, {"isolate_prepare", true}}},
        {"objects", json::array({object})}};
    importBase_ = snapshot;
    status_->setText(tr("后台准备中…编辑器可继续查看，场景变化后将拒绝过期导入"));
    import_.emplace(earth::VoxelScene::prepareImportAsync(document));
}
} // namespace vsgraysim::ui
