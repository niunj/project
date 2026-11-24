#pragma once
/// @file EquipmentLinkDialog.h
/// @brief 装备链接对话框：显控软件启动时弹出，扫描在线装备并选择链接。

#include <QDialog>
#include <QPushButton>
#include <QTableWidget>

#include "vsgraysim/core/SensorTypes.h"

namespace vsgraysim { namespace gui {

class EquipmentLinkDialog : public QDialog {
    Q_OBJECT
public:
    explicit EquipmentLinkDialog(SensorType filter_type, QWidget* parent = nullptr);

    uint32_t selectedEquipmentId() const { return selected_id_; }
    std::string selectedEquipmentName() const { return selected_name_; }

private slots:
    void refreshEquipmentList();
    void onSelectionChanged();

private:
    void buildUi();

    SensorType filter_type_;
    uint32_t selected_id_ = 0;
    std::string selected_name_;

    QTableWidget* table_ = nullptr;
    QPushButton* btn_refresh_ = nullptr;
    QPushButton* btn_connect_ = nullptr;
};

}} // namespace vsgraysim::gui
