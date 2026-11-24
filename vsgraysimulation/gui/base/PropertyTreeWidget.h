#pragma once
/// @file PropertyTreeWidget.h
/// @brief 界面公共模块: 属性参数分组树 (QTreeWidget)。
///
/// 统一承载「场景树节点 → 属性参数」的展示与编辑, 三个软件共用:
///   - 分组父节点 (粗体/可折叠) + 属性叶子 (名称 | 值);
///   - 由 prop::Group/Item (PropertySchema) 声明式驱动, 各软件只传取值回调;
///   - 场景设计软件 editable=true 时树内直接编辑, 编辑实时回写 (valueEdited);
///   - 仿真控制软件 editable=false 时为只读树;
///   - Runtime/Derived 字段以强调色区分静态配置字段。
#ifndef VSGRAYSIM_GUI_BASE_PROPERTYTREEWIDGET_H
#define VSGRAYSIM_GUI_BASE_PROPERTYTREEWIDGET_H

#include <QTreeWidget>
#include <QVector>

#include <functional>

#include "PropertySchema.h"

namespace vsgraysim { namespace gui { namespace base {

class PropertyTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit PropertyTreeWidget(QWidget* parent = nullptr);

    /// 清空并用分组定义重建: 每 items 为分组父节点, item 为叶子 (名称|值)。
    /// @param valueOf 每字段取值回调 (入参 Item, 返回展示文本)。
    void setGroups(const QVector<prop::Group>& groups,
                   const std::function<QString(const prop::Item&)>& valueOf);

    /// 更新单个字段展示值 (不触发编辑信号), 找不到则忽略。
    void setItemValue(const QString& key, const QString& text);

    /// 切换是否可编辑 (树内直接改值)。只读时不可点击编辑。
    void setEditable(bool on);

    /// 返回某字段当前展示文本。
    QString itemValue(const QString& key) const;

signals:
    /// 可编辑态下用户改完某字段: 携带 分组id / 字段key / 新值文本。
    void valueEdited(const QString& groupId, const QString& key,
                     const QString& text);

private:
    void onItemChanged(QTreeWidgetItem* item, int column);

    // 树项 UserRole 常量
    enum Role {
        RoleKey     = Qt::UserRole,     ///< 字段 key
        RoleGroupId = Qt::UserRole + 1, ///< 所属分组 id
    };

    bool editable_ = false;
};

}}} // namespace vsgraysim::gui::base
#endif // VSGRAYSIM_GUI_BASE_PROPERTYTREEWIDGET_H