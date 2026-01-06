#include "NodePropertyWidget.h"
#include <QHeaderView>
#include <QFont>
#include <QList>


#include "../Log/log_manager.h"

NodePropertyWidget::NodePropertyWidget(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* lo = new QVBoxLayout(this);
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels(QStringList() << "属性" << "值");
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    lo->addWidget(tree_);
    setLayout(lo);



    //当服务选中平台变化时,让属性面板显示对应平台（-1 表示清空）
    // connect(&Mte3DService::getInstance(), &Mte3DService::sig_platformSelected,
    //         this, [=](int platformID){
    //     //不能从三维获取
    //     //只能从CommonFunction 获取
    //     // if(platformID != -1) {
    //     //     setPlatform(CommonFunction::getInstance().getPlatform(platformID));
    //     // }
    // });


    // 当平台变换时（拖拽/缩放/程序修改），刷新属性面板(直接请求最新平台数据)
    // connect(&Mte3DService::getInstance(), &Mte3DService::sig_platformTransformChanged,
    //         this, [=](int platformID, double lon, double lat , double alt, double head, double pitch , double roll, double scale){
    //     // 只要平台ID有效，刷新属性显示

    //     //不能从三维获取
    //     //只能从CommonFunction 获取
    //     LOG_INFO <<platformID<< lon<< lat << alt<< head<< pitch << roll<< scale;
    //     // CommonFunction::getInstance().setPlatformInfo(platformID, QVector3D(lon, lat, alt), QVector3D(head, pitch, roll), scale );

    //     // setPlatform(CommonFunction::getInstance().getPlatform(platformID));
    // });

}

void NodePropertyWidget::clear()
{
    tree_->clear();
}

QString NodePropertyWidget::safeToString(const QVariant& v)
{
    if (!v.isValid()) return QString();
    return v.toString();
}

void NodePropertyWidget::addGroup(const QString& title, const QList<QPair<QString, QString>>& props)
{
    QTreeWidgetItem* parent = new QTreeWidgetItem(tree_);
    parent->setText(0, title);
    QFont f = parent->font(0);
    f.setBold(true);
    parent->setFont(0, f);
    // add children
    for (const auto& p : props) {
        QTreeWidgetItem* child = new QTreeWidgetItem(parent);
        child->setText(0, p.first);
        child->setText(1, p.second);
    }
    parent->setExpanded(true);
}


void NodePropertyWidget::setPlatform(const MtePlatformStru& plat)
{
    clear();

    if (plat.m_id < 0) return;

    // 从服务获取平台信息
    // ---------------- General ----------------
    QList<QPair<QString, QString>> general;
    general.append({"编号", QString::number(plat.m_id)});
    general.append({"名称", plat.m_name});
//    general.append({"路径", plat.m_path});

    QString modelTypeStr = "Unknown";
    switch (plat.modelType)
    {
    case OBJECT: modelTypeStr = "OBJECT"; break;
    case BACKGROUND: modelTypeStr = "BACKGROUND"; break;
    case GEO: modelTypeStr = "GEO"; break;
    default: modelTypeStr = QString::number(static_cast<int>(plat.modelType)); break;
    }
    general.append({"模型类型", modelTypeStr});
    addGroup("Generate", general);

    // ---------------- Transform / Attributes ----------------
    QList<QPair<QString, QString>> trans;
    trans.append({"x", QString::number(plat.m_attribute.m_x, 'f', 6)});
    trans.append({"y", QString::number(plat.m_attribute.m_y, 'f', 6)});
    trans.append({"z", QString::number(plat.m_attribute.m_z, 'f', 3)});
    trans.append({"航向(Heading)", QString::number(plat.m_attribute.m_h, 'f', 3)});
    trans.append({"俯仰(Pitch)", QString::number(plat.m_attribute.m_p, 'f', 3)});
    trans.append({"横滚(Roll)", QString::number(plat.m_attribute.m_r, 'f', 3)});
    trans.append({"缩放(Scale)", QString::number(plat.m_attribute.m_scale, 'f', 6)});
    addGroup("Transform", trans);



}
