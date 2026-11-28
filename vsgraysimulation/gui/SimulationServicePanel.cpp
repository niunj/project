#include "SimulationServicePanel.h"
#include "SimulationWorkerClient.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <nlohmann/json.hpp>

namespace vsgraysim::gui {
namespace {
using Json = nlohmann::json;
uint32_t selected(const QComboBox* box) { return box->currentData().toUInt(); }

void populate(QComboBox* box, const Json& items, const char* key, const QString& placeholder) {
    const auto previous = selected(box);
    bool same = box->count() == int(items.size()) + 1;
    int index = 1;
    for (const auto& row : items) {
        const auto id = row.at(key).get<uint32_t>();
        if (!same || box->itemData(index).toUInt() != id) same = false;
        ++index;
    }
    if (same) return;
    const QSignalBlocker blocker(box);
    box->clear();
    box->addItem(placeholder, 0);
    for (const auto& row : items)
        box->addItem(QString::fromStdString(row.at("name").get<std::string>()), row.at(key).get<uint32_t>());
    box->setCurrentIndex(std::max(0, box->findData(previous)));
}
}

SimulationServicePanel::SimulationServicePanel(SimulationWorkerClient& client, QWidget* parent)
    : QWidget(parent), client_(client) {
    auto* layout = new QFormLayout(this);
    platforms_ = new QComboBox(this);
    platforms_->setObjectName("servicePlatforms");
    equipment_ = new QComboBox(this);
    equipment_->setObjectName("serviceEquipment");
    heading_ = new QDoubleSpinBox(this);
    heading_->setObjectName("serviceHeading");
    heading_->setRange(0, 359.99);
    speed_ = new QDoubleSpinBox(this);
    speed_->setObjectName("serviceSpeed");
    state_ = new QLabel(this);
    state_->setObjectName("servicePlatformState");
    state_->setWordWrap(true);
    image_ = new QLabel(this);
    image_->setObjectName("serviceProductImage");
    image_->setMinimumWidth(240);
    values_ = new QLabel(this);
    values_->setObjectName("serviceProductValues");
    values_->setTextFormat(Qt::PlainText);
    values_->setWordWrap(true);
    notice_ = new QLabel(this);
    notice_->setObjectName("serviceCommandNotice");
    notice_->setWordWrap(true);
    apply_ = new QPushButton(tr("提交航向与航速"), this);
    apply_->setObjectName("serviceSendCourse");
    apply_->setEnabled(false);
    layout->addRow(tr("平台"), platforms_);
    layout->addRow(state_);
    layout->addRow(tr("航向 (°)"), heading_);
    layout->addRow(tr("航速 (m/s)"), speed_);
    layout->addRow(apply_);
    layout->addRow(tr("装备"), equipment_);
    layout->addRow(values_);
    layout->addRow(image_);
    layout->addRow(notice_);
    connect(platforms_, &QComboBox::currentIndexChanged, this, [this] { updateStatus(client_.status()); });
    connect(equipment_, &QComboBox::currentIndexChanged, this, [this] {
        image_->clear();
        values_->setText(tr("等待装备帧"));
        values_->setProperty("equipmentId", 0);
        client_.selectEquipment(selected(equipment_));
    });
    connect(apply_, &QPushButton::clicked, this, [this] {
        client_.setCourse(selected(platforms_), heading_->value(), speed_->value());
    });
    client_.productChanged = [this](const auto& product) { updateProduct(product); };
    client_.serviceNotice = [this](const auto& notice) { notice_->setText(notice); };
}

void SimulationServicePanel::updateStatus(const Json& status) {
    const auto platforms = status.value("platforms", Json::array());
    populate(platforms_, platforms, "platform_id", tr("选择平台"));
    auto previews = Json::array();
    for (const auto& unit : status.value("equipment", Json::array())) {
        if (unit.value("preview_supported", false)) previews.push_back(unit);
    }
    populate(equipment_, previews, "id", tr("选择装备"));
    client_.selectEquipment(selected(equipment_));
    if (!selected(equipment_)) {
        image_->clear();
        values_->clear();
    }
    const auto id = selected(platforms_);
    const auto controls = status.value("prescribed_controls", Json::object());
    const auto command = controls.find(std::to_string(id));
    const bool allowed = status.value("initialized", false) && command != controls.end();
    apply_->setEnabled(allowed);
    heading_->setEnabled(allowed);
    speed_->setEnabled(allowed);
    if (allowed) speed_->setMaximum(command->at("max_speed_mps").get<double>());
    state_->clear();
    for (const auto& platform : platforms) if (platform.at("platform_id") == id) {
        QStringList lines;
        if (platform.contains("yaw") && platform.contains("speed"))
            lines << tr("姿态航向 %1° · 水平速度 %2 m/s").arg(platform.at("yaw").get<double>(),0,'f',1)
                .arg(platform.at("speed").get<double>(),0,'f',2);
        if (platform.contains("latitude") && platform.contains("longitude"))
            lines << tr("纬度 %1° · 经度 %2°").arg(platform.at("latitude").get<double>(),0,'f',6)
                .arg(platform.at("longitude").get<double>(),0,'f',6);
        state_->setText(lines.join('\n'));
        break;
    }
}

void SimulationServicePanel::updateProduct(const Json& product) {
    const auto kind = product.at("kind").get<std::string>();
    auto details = tr("平台 %1 · 装备 %2 · t=%3 s").arg(product.at("platform_id").get<uint32_t>())
        .arg(product.at("equipment_id").get<uint32_t>()).arg(product.at("sim_time_s").get<double>(),0,'f',2);
    if (kind == "navigation") {
        const auto& p = product.at("navigation");
        details += tr("\n纬度 %1° · 经度 %2° · 椭球高 %3 m")
            .arg(p.at("latitude_deg").get<double>(),0,'f',6).arg(p.at("longitude_deg").get<double>(),0,'f',6)
            .arg(p.at("ellipsoid_height_m").get<double>(),0,'f',2);
    }
    if (kind == "depth_sounder") {
        if (product.at("bottom") == 0)
            details += tr("\n水深 %1 m · 双程 %2 ms").arg(product.at("water_depth_m").get<double>(),0,'f',2)
                .arg(product.at("two_way_ms").get<double>(),0,'f',2);
        else
            details += product.at("bottom") == 1 ? tr("\n换能器离水") : tr("\n未收到海底回波");
    }
    if (product.contains("image") && product.at("image").is_object()) {
        const auto& frame = product.at("image");
        const int w = frame.at("width").get<int>(), h = frame.at("height").get<int>();
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(frame.at("pixels_base64").get<std::string>()));
        if (w > 0 && h > 0 && w <= 512 && h <= 512 && bytes.size() == qsizetype(w) * h) {
            const QImage img(reinterpret_cast<const uchar*>(bytes.constData()), w, h, w, QImage::Format_Grayscale8);
            image_->setPixmap(QPixmap::fromImage(img.copy()).scaled(320, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        details += tr("\n采集阶段 P%1 · %2 × %3").arg(product.at("stage").get<int>())
            .arg(frame.at("source_width").get<int>()).arg(frame.at("source_height").get<int>());
    }
    values_->setText(details);
    values_->setProperty("equipmentId", product.at("equipment_id").get<uint32_t>());
    values_->setProperty("platformId", product.at("platform_id").get<uint32_t>());
    values_->setProperty("frameId", QString::fromStdString(product.at("frame_id").get<std::string>()));
}
}
