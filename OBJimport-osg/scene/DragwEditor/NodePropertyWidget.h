#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "../Common/MteStructDef.h"
#include "../OsgRender/Mte3DService.h"

class NodePropertyWidget : public QWidget
{
    Q_OBJECT
public:
    explicit NodePropertyWidget(QWidget* parent = nullptr);

public slots:
    // 设置要展示的平台 id（-1 清空）
    void setPlatform(const MtePlatformStru& );
    void clear();

private:
    QTreeWidget* tree_;
    void addGroup(const QString& title, const QList<QPair<QString, QString>>& props);
    QString safeToString(const QVariant& v);
};
