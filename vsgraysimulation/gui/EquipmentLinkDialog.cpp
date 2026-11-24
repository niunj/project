/// @file EquipmentLinkDialog.cpp
/// @brief 装备链接对话框实现。

#include "EquipmentLinkDialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>

#include "vsgraysim/broker/SimDataBroker.h"
#include "vsgraysim/core/SensorTypes.h"

namespace vsgraysim { namespace gui {

namespace {
const char* kDialogQss = R"QSS(
QDialog { background: #1a1d24; color: #d7dbe2; }
QLabel { color: #c6ccd8; }
QTableWidget { background: #10131a; color: #d7dbe2; border: 1px solid #2a3040;
               border-radius: 4px; gridline-color: #2a3040; }
QTableWidget::item { padding: 4px; }
QTableWidget::item:selected { background: #2d6fb8; color: white; }
QHeaderView::section { background: #14161c; color: #9aa5b8;
                       border: 1px solid #2a3040; padding: 3px 6px; }
QPushButton { background: #243044; color: #dbe4f0; border: 1px solid #34425c;
              border-radius: 4px; padding: 5px 12px; }
QPushButton:hover { background: #2e3d57; }
QPushButton:pressed { background: #3a4c6e; }
QPushButton:disabled { color: #5d6675; background: #1d222c; }
QPushButton#primary { background: #1d4f8a; }
QPushButton#primary:hover { background: #2560a8; }
QPushButton#primary:disabled { background: #1a2a3f; }
)QSS";
} // anonymous namespace

EquipmentLinkDialog::EquipmentLinkDialog(SensorType filter_type, QWidget* parent)
    : QDialog(parent)
    , filter_type_(filter_type)
{
    setStyleSheet(QString::fromUtf8(kDialogQss));
    buildUi();
    refreshEquipmentList();
}

void EquipmentLinkDialog::buildUi()
{
    setWindowTitle(tr("链接装备"));
    resize(680, 420);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    // 标题
    auto* lbl_title = new QLabel(tr("选择要链接的装备"), this);
    lbl_title->setStyleSheet("font-size: 16px; font-weight: 600; color: #e8ecf3;");
    layout->addWidget(lbl_title);

    // 提示
    QString type_name;
    switch (filter_type_) {
    case SensorType::RADAR: type_name = tr("雷达"); break;
    case SensorType::SAR:   type_name = tr("SAR"); break;
    case SensorType::IR:    type_name = tr("红外"); break;
    case SensorType::VIS:   type_name = tr("可见光"); break;
    case SensorType::LASER: type_name = tr("激光"); break;
    }
    auto* lbl_hint = new QLabel(
        tr("仿真软件在线的%1装备列表。选择一个装备进行链接。").arg(type_name), this);
    lbl_hint->setStyleSheet("font-size: 12px; color: #8b95a6;");
    layout->addWidget(lbl_hint);

    // 装备列表
    table_ = new QTableWidget(this);
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({
        tr("装备ID"), tr("装备名称"), tr("类型"),
        tr("所属平台"), tr("帧率(Hz)"), tr("状态")
    });
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &EquipmentLinkDialog::onSelectionChanged);
    layout->addWidget(table_, 1);

    // 按钮行
    auto* btn_layout = new QHBoxLayout;
    btn_layout->setSpacing(8);

    btn_refresh_ = new QPushButton(tr("刷新列表"), this);
    connect(btn_refresh_, &QPushButton::clicked,
            this, &EquipmentLinkDialog::refreshEquipmentList);
    btn_layout->addWidget(btn_refresh_);

    btn_layout->addStretch();

    auto* btn_cancel = new QPushButton(tr("取消"), this);
    connect(btn_cancel, &QPushButton::clicked, this, &QDialog::reject);
    btn_layout->addWidget(btn_cancel);

    btn_connect_ = new QPushButton(tr("链接"), this);
    btn_connect_->setObjectName("primary");
    btn_connect_->setEnabled(false);
    connect(btn_connect_, &QPushButton::clicked, this, &QDialog::accept);
    btn_layout->addWidget(btn_connect_);

    layout->addLayout(btn_layout);
}

void EquipmentLinkDialog::refreshEquipmentList()
{
    table_->setRowCount(0);

    auto& broker = SimDataBroker::instance();
    auto equipment_list = broker.scanByType(filter_type_);

    if (equipment_list.empty()) {
        // 无装备时显示提示
        table_->setRowCount(1);
        auto* item = new QTableWidgetItem(tr("(仿真软件未启动或无在线装备)"));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setForeground(QColor("#5d6675"));
        table_->setItem(0, 0, item);
        table_->setSpan(0, 0, 1, 6);
        btn_connect_->setEnabled(false);
        return;
    }

    for (const auto& eq : equipment_list) {
        int row = table_->rowCount();
        table_->insertRow(row);

        table_->setItem(row, 0, new QTableWidgetItem(QString::number(eq.equipment_id)));
        table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(eq.name)));
        table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(sensorTypeName(eq.type))));
        table_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(eq.platform_name)));
        table_->setItem(row, 4, new QTableWidgetItem(QString::number(eq.frame_rate, 'f', 1)));

        QString status = eq.powered ? tr("开机") : tr("关机");
        if (!eq.active) status += tr(" (未活跃)");
        auto* status_item = new QTableWidgetItem(status);
        if (eq.powered) {
            status_item->setForeground(QColor("#3fb68b"));
        } else {
            status_item->setForeground(QColor("#e74c3c"));
        }
        table_->setItem(row, 5, status_item);
    }

    spdlog::info("EquipmentLinkDialog: refreshed, found {} equipment of type {}",
                 equipment_list.size(), sensorTypeName(filter_type_));
}

void EquipmentLinkDialog::onSelectionChanged()
{
    auto selected = table_->selectedItems();
    if (selected.isEmpty()) {
        btn_connect_->setEnabled(false);
        selected_id_ = 0;
        selected_name_.clear();
        return;
    }

    int row = selected.first()->row();
    auto* id_item = table_->item(row, 0);
    auto* name_item = table_->item(row, 1);

    if (id_item && name_item) {
        selected_id_ = id_item->text().toUInt();
        selected_name_ = name_item->text().toStdString();
        btn_connect_->setEnabled(true);
    }
}

}} // namespace vsgraysim::gui
