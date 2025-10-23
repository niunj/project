#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QDateEdit>
#include <QTimeEdit>
#include <QDateTimeEdit>
#include <QColorDialog>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>
#include "qpropertyobject.h"
#include "qpropertyvalue.h"

/**
 * @brief 属性显示控件类
 * 用于可视化显示QPropertyObject及其所有属性和子对象
 */
class PropertyWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父控件
     */
    explicit PropertyWidget(QWidget *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~PropertyWidget();
    
    /**
     * @brief 设置要显示的属性对象
     * @param propertyObject 属性对象指针
     */
    void setPropertyObject(QPropertyObject *propertyObject);
    
    /**
     * @brief 获取当前显示的属性对象
     * @return 属性对象指针
     */
    QPropertyObject *propertyObject() const;
    
    /**
     * @brief 设置是否自动刷新
     * @param autoRefresh 是否自动刷新
     */
    void setAutoRefresh(bool autoRefresh);
    
    /**
     * @brief 获取是否自动刷新
     * @return 是否自动刷新
     */
    bool autoRefresh() const;
    
    /**
     * @brief 手动刷新显示
     */
    void refresh();
    
    /**
     * @brief 设置是否显示只读属性
     * @param showReadOnly 是否显示只读属性
     */
    void setShowReadOnlyProperties(bool showReadOnly);
    
    /**
     * @brief 设置是否按组显示
     * @param groupByType 是否按类型分组
     */
    void setGroupByType(bool groupByType);
    
    /**
     * @brief 设置是否显示高级属性
     * @param showAdvanced 是否显示高级属性
     */
    void setShowAdvancedProperties(bool showAdvanced);

private slots:
    /**
     * @brief 属性值变更槽函数
     * @param propertyName 属性名称
     * @param value 新值
     */
    void onPropertyValueChanged(const QString &propertyName, const QVariant &value);
    
    /**
     * @brief 子对象变更槽函数
     * @param objectName 对象名称
     * @param object 对象指针
     */
    void onChildObjectChanged(const QString &objectName, QPropertyObject *object);
    
    /**
     * @brief 刷新定时器槽函数
     */
    void onRefreshTimer();
    
    /**
     * @brief 颜色选择按钮点击槽函数
     */
    void onColorButtonClicked();
    
    /**
     * @brief 树节点展开槽函数
     * @param index 索引
     */
    void onTreeItemExpanded(const QModelIndex &index);
    
    /**
     * @brief 树节点双击槽函数
     * @param item 树节点项
     * @param column 列索引
     */
    void onTreeItemDoubleClicked(QTreeWidgetItem *item, int column);
    
    /**
     * @brief 编辑控件值变更槽函数
     */
    void onEditorValueChanged();

private:
    /**
     * @brief 创建基本类型的编辑器
     * @param value 属性值对象
     * @param parent 父控件
     * @return 编辑控件
     */
    QWidget *createEditorForProperty(QPropertyValue *value, QWidget *parent = nullptr);
    
    /**
     * @brief 创建列表类型的编辑器
     * @param value 属性值对象
     * @param parent 父控件
     * @return 编辑控件
     */
    QWidget *createListEditor(QPropertyValue *value, QWidget *parent = nullptr);
    
    /**
     * @brief 创建映射类型的编辑器
     * @param value 属性值对象
     * @param parent 父控件
     * @return 编辑控件
     */
    QWidget *createMapEditor(QPropertyValue *value, QWidget *parent = nullptr);
    
    /**
     * @brief 递归构建属性树
     * @param parentItem 父树节点
     * @param object 属性对象
     * @param level 层级
     */
    void buildPropertyTree(QTreeWidgetItem *parentItem, QPropertyObject *object, int level = 0);
    
    /**
     * @brief 添加属性到树节点
     * @param parentItem 父树节点
     * @param value 属性值对象
     */
    void addPropertyToTree(QTreeWidgetItem *parentItem, QPropertyValue *value);
    
    /**
     * @brief 更新属性编辑区域
     * @param item 当前选中的树节点
     */
    void updatePropertyEditor(QTreeWidgetItem *item);
    
    /**
     * @brief 清理编辑区域
     */
    void clearPropertyEditor();
    
    /**
     * @brief 获取属性显示名称
     * @param value 属性值对象
     * @return 显示名称
     */
    QString getPropertyDisplayName(QPropertyValue *value);
    
    /**
     * @brief 获取属性类型显示名称
     * @param type 属性类型
     * @return 类型显示名称
     */
    QString getPropertyTypeDisplayName(PropertyValueType type);
    
    /**
     * @brief 为树节点设置图标
     * @param item 树节点
     * @param type 属性类型
     */
    void setItemIcon(QTreeWidgetItem *item, PropertyValueType type);
    
    /**
     * @brief 为树节点设置图标（对象类型）
     * @param item 树节点
     */
    void setObjectItemIcon(QTreeWidgetItem *item);
    
    /**
     * @brief 保存当前属性状态
     */
    void saveCurrentState();
    
    /**
     * @brief 恢复属性状态
     */
    void restoreState();

    // 成员变量
    QTreeWidget *m_treeWidget;          // 属性树
    QWidget *m_propertyEditorWidget;    // 属性编辑区域
    QVBoxLayout *m_mainLayout;          // 主布局
    QVBoxLayout *m_editorLayout;        // 编辑器布局
    QPropertyObject *m_propertyObject;  // 当前属性对象
    bool m_autoRefresh;                 // 是否自动刷新
    QTimer *m_refreshTimer;             // 刷新定时器
    bool m_showReadOnlyProperties;      // 是否显示只读属性
    bool m_groupByType;                 // 是否按类型分组
    bool m_showAdvancedProperties;      // 是否显示高级属性
    QMap<QWidget *, QPropertyValue *> m_editorToPropertyMap;  // 编辑器到属性的映射
    QMap<QString, QVariant> m_lastState;  // 上次状态
    bool m_updating;  // 是否正在更新中，防止循环更新
};