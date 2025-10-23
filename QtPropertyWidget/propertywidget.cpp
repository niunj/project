#include "propertywidget.h"
#include <QTimer>
#include <QDebug>
#include <QScrollArea>
#include <QFont>
#include <QColor>
#include <QPalette>
#include <QSizePolicy>
#include <QMessageBox>

PropertyWidget::PropertyWidget(QWidget *parent)
    : QWidget(parent),
      m_treeWidget(new QTreeWidget(this)),
      m_propertyEditorWidget(new QWidget(this)),
      m_mainLayout(new QVBoxLayout(this)),
      m_editorLayout(new QVBoxLayout(m_propertyEditorWidget)),
      m_propertyObject(nullptr),
      m_autoRefresh(true),
      m_refreshTimer(new QTimer(this)),
      m_showReadOnlyProperties(true),
      m_groupByType(true),
      m_showAdvancedProperties(false),
      m_updating(false)
{
    // 初始化树控件
    m_treeWidget->setColumnCount(3);
    m_treeWidget->setHeaderLabels({tr("名称"), tr("值"), tr("类型")});
    m_treeWidget->setAlternatingRowColors(true);
    m_treeWidget->setSortingEnabled(false);
    m_treeWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    // 设置编辑区域
    m_propertyEditorWidget->setLayout(m_editorLayout);
    m_editorLayout->setContentsMargins(5, 5, 5, 5);
    m_editorLayout->setSpacing(5);
    
    // 创建滚动区域
    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidget(m_propertyEditorWidget);
    scrollArea->setWidgetResizable(true);
    
    // 设置主布局
    m_mainLayout->setContentsMargins(5, 5, 5, 5);
    m_mainLayout->setSpacing(5);
    
    // 创建分割器
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_treeWidget);
    splitter->addWidget(scrollArea);
    splitter->setSizes({200, 400}); // 设置初始大小
    
    m_mainLayout->addWidget(splitter);
    setLayout(m_mainLayout);
    
    // 连接信号槽
    connect(m_treeWidget, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current) {
        updatePropertyEditor(current);
    });
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &PropertyWidget::onTreeItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::itemExpanded, this, &PropertyWidget::onTreeItemExpanded);
    
    // 设置刷新定时器
    connect(m_refreshTimer, &QTimer::timeout, this, &PropertyWidget::onRefreshTimer);
    m_refreshTimer->start(500); // 500ms刷新一次
    
    // 设置样式
    setStyleSheet(
        "QTreeWidget { border: 1px solid #ccc; border-radius: 3px; }"
        "QTreeWidget::item { padding: 2px; }"
        "QTreeWidget::item:selected { background-color: #cce8ff; }"
        "QGroupBox { border: 1px solid #ddd; border-radius: 4px; margin-top: 5px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 3px 0 3px; }"
    );
}

PropertyWidget::~PropertyWidget()
{
    // 清理资源
    clearPropertyEditor();
    m_refreshTimer->stop();
    disconnect(m_propertyObject);
}

void PropertyWidget::setPropertyObject(QPropertyObject *propertyObject)
{
    if (m_propertyObject == propertyObject) {
        return;
    }
    
    // 断开旧对象的连接
    if (m_propertyObject) {
        disconnect(m_propertyObject);
    }
    
    m_propertyObject = propertyObject;
    
    // 连接新对象的信号
    if (m_propertyObject) {
        // 连接属性变更信号
        // 注意：这里需要在QPropertyObject中添加相应的信号
    }
    
    // 刷新显示
    refresh();
}

QPropertyObject *PropertyWidget::propertyObject() const
{
    return m_propertyObject;
}

void PropertyWidget::setAutoRefresh(bool autoRefresh)
{
    m_autoRefresh = autoRefresh;
    if (autoRefresh) {
        m_refreshTimer->start(500);
    } else {
        m_refreshTimer->stop();
    }
}

bool PropertyWidget::autoRefresh() const
{
    return m_autoRefresh;
}

void PropertyWidget::refresh()
{
    if (m_updating || !m_propertyObject) {
        return;
    }
    
    m_updating = true;
    
    // 保存当前展开状态
    QList<QTreeWidgetItem *> expandedItems;
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_treeWidget->topLevelItem(i);
        if (item->isExpanded()) {
            expandedItems.append(item);
        }
    }
    
    // 清空树
    m_treeWidget->clear();
    
    // 重建属性树
    QTreeWidgetItem *rootItem = new QTreeWidgetItem(m_treeWidget);
    rootItem->setText(0, m_propertyObject->name());
    rootItem->setText(2, "Object");
    setObjectItemIcon(rootItem);
    
    buildPropertyTree(rootItem, m_propertyObject, 1);
    
    // 恢复展开状态
    for (QTreeWidgetItem *item : expandedItems) {
        QTreeWidgetItem *found = nullptr;
        // 查找对应的项并展开
        // 这里简化处理，实际应用中可能需要更复杂的查找逻辑
    }
    
    rootItem->setExpanded(true);
    
    m_updating = false;
}

void PropertyWidget::setShowReadOnlyProperties(bool showReadOnly)
{
    m_showReadOnlyProperties = showReadOnly;
    refresh();
}

void PropertyWidget::setGroupByType(bool groupByType)
{
    m_groupByType = groupByType;
    refresh();
}

void PropertyWidget::setShowAdvancedProperties(bool showAdvanced)
{
    m_showAdvancedProperties = showAdvanced;
    refresh();
}

void PropertyWidget::onPropertyValueChanged(const QString &propertyName, const QVariant &value)
{
    // 当属性值变更时更新UI
    if (m_updating) {
        return;
    }
    
    // 查找对应的树节点并更新值
    // 这里需要实现查找逻辑
    
    // 如果当前正在编辑该属性，更新编辑器
    for (auto it = m_editorToPropertyMap.constBegin(); it != m_editorToPropertyMap.constEnd(); ++it) {
        if (it.value() && it.value()->name() == propertyName) {
            // 更新编辑器的值
            updateEditorValue(it.key(), value);
            break;
        }
    }
}

void PropertyWidget::onChildObjectChanged(const QString &objectName, QPropertyObject *object)
{
    // 当子对象变更时更新UI
    refresh();
}

void PropertyWidget::onRefreshTimer()
{
    if (m_autoRefresh && m_propertyObject) {
        // 比较当前状态与上次状态，如果有变化则刷新
        QMap<QString, QVariant> currentState = getCurrentState();
        if (currentState != m_lastState) {
            m_lastState = currentState;
            refresh();
        }
    }
}

void PropertyWidget::onColorButtonClicked()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (!button) {
        return;
    }
    
    // 查找对应的属性
    for (auto it = m_editorToPropertyMap.constBegin(); it != m_editorToPropertyMap.constEnd(); ++it) {
        if (it.key() == button && it.value()) {
            QColor currentColor = it.value()->value().value<QColor>();
            QColor newColor = QColorDialog::getColor(currentColor, this, tr("选择颜色"));
            
            if (newColor.isValid() && newColor != currentColor) {
                m_updating = true;
                it.value()->setValue(newColor);
                button->setStyleSheet(QString("background-color: %1; color: %2;").arg(
                    newColor.name(), 
                    (newColor.lightness() < 128) ? "white" : "black"
                ));
                m_updating = false;
            }
            break;
        }
    }
}

void PropertyWidget::onTreeItemExpanded(const QModelIndex &index)
{
    // 当树节点展开时，可以延迟加载子节点以提高性能
    QTreeWidgetItem *item = m_treeWidget->itemFromIndex(index);
    if (item && item->data(0, Qt::UserRole).canConvert<QPropertyObject*>()) {
        QPropertyObject *object = item->data(0, Qt::UserRole).value<QPropertyObject*>();
        if (object && item->childCount() == 0) {
            buildPropertyTree(item, object, index.row() + 1);
        }
    }
}

void PropertyWidget::onTreeItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    if (!item || column != 1) {
        return;
    }
    
    // 处理双击事件，例如打开复杂类型的编辑对话框
    if (item->data(0, Qt::UserRole + 1).canConvert<QPropertyValue*>()) {
        QPropertyValue *value = item->data(0, Qt::UserRole + 1).value<QPropertyValue*>();
        if (value) {
            // 根据不同类型显示不同的编辑对话框
            switch (value->type()) {
            case PropertyValueType::List:
            case PropertyValueType::Map:
                // 显示列表或映射编辑对话框
                break;
            default:
                break;
            }
        }
    }
}

void PropertyWidget::onEditorValueChanged()
{
    if (m_updating) {
        return;
    }
    
    m_updating = true;
    
    // 获取发送信号的编辑器
    QWidget *editor = qobject_cast<QWidget *>(sender());
    if (!editor) {
        m_updating = false;
        return;
    }
    
    // 查找对应的属性
    if (m_editorToPropertyMap.contains(editor)) {
        QPropertyValue *property = m_editorToPropertyMap[editor];
        if (property) {
            // 根据编辑器类型获取值
            QVariant value;
            
            if (QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor)) {
                value = lineEdit->text();
            } else if (QCheckBox *checkBox = qobject_cast<QCheckBox *>(editor)) {
                value = checkBox->isChecked();
            } else if (QSpinBox *spinBox = qobject_cast<QSpinBox *>(editor)) {
                value = spinBox->value();
            } else if (QDoubleSpinBox *doubleSpinBox = qobject_cast<QDoubleSpinBox *>(editor)) {
                value = doubleSpinBox->value();
            } else if (QComboBox *comboBox = qobject_cast<QComboBox *>(editor)) {
                value = comboBox->currentText();
            } else if (QDateEdit *dateEdit = qobject_cast<QDateEdit *>(editor)) {
                value = dateEdit->date();
            } else if (QTimeEdit *timeEdit = qobject_cast<QTimeEdit *>(editor)) {
                value = timeEdit->time();
            } else if (QDateTimeEdit *dateTimeEdit = qobject_cast<QDateTimeEdit *>(editor)) {
                value = dateTimeEdit->dateTime();
            }
            
            // 设置属性值
            property->setValue(value);
            
            // 更新树中的显示
            updateTreeItemValue(property);
        }
    }
    
    m_updating = false;
}

QWidget *PropertyWidget::createEditorForProperty(QPropertyValue *value, QWidget *parent)
{
    if (!value) {
        return nullptr;
    }
    
    QWidget *editor = nullptr;
    
    switch (value->type()) {
    case PropertyValueType::Bool:
    {
        QCheckBox *checkBox = new QCheckBox(parent);
        checkBox->setChecked(value->value().toBool());
        connect(checkBox, &QCheckBox::toggled, this, &PropertyWidget::onEditorValueChanged);
        editor = checkBox;
        break;
    }
    case PropertyValueType::Int:
    {
        QSpinBox *spinBox = new QSpinBox(parent);
        spinBox->setValue(value->value().toInt());
        if (value->minValue().isValid()) {
            spinBox->setMinimum(value->minValue().toInt());
        }
        if (value->maxValue().isValid()) {
            spinBox->setMaximum(value->maxValue().toInt());
        }
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &PropertyWidget::onEditorValueChanged);
        editor = spinBox;
        break;
    }
    case PropertyValueType::Float:
    case PropertyValueType::Double:
    {
        QDoubleSpinBox *doubleSpinBox = new QDoubleSpinBox(parent);
        doubleSpinBox->setValue(value->value().toDouble());
        doubleSpinBox->setDecimals(value->precision() > 0 ? value->precision() : 2);
        if (value->minValue().isValid()) {
            doubleSpinBox->setMinimum(value->minValue().toDouble());
        }
        if (value->maxValue().isValid()) {
            doubleSpinBox->setMaximum(value->maxValue().toDouble());
        }
        connect(doubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertyWidget::onEditorValueChanged);
        editor = doubleSpinBox;
        break;
    }
    case PropertyValueType::String:
    case PropertyValueType::Char:
    {
        QLineEdit *lineEdit = new QLineEdit(parent);
        lineEdit->setText(value->value().toString());
        connect(lineEdit, &QLineEdit::textChanged, this, &PropertyWidget::onEditorValueChanged);
        editor = lineEdit;
        break;
    }
    case PropertyValueType::Enum:
    {
        QComboBox *comboBox = new QComboBox(parent);
        // 这里需要从属性的元数据中获取枚举选项
        // 简化处理，实际应用中需要更复杂的逻辑
        comboBox->addItem(value->value().toString());
        comboBox->setCurrentText(value->value().toString());
        connect(comboBox, &QComboBox::currentTextChanged, this, &PropertyWidget::onEditorValueChanged);
        editor = comboBox;
        break;
    }
    case PropertyValueType::Date:
    {
        QDateEdit *dateEdit = new QDateEdit(parent);
        dateEdit->setDate(value->value().toDate());
        connect(dateEdit, &QDateEdit::dateChanged, this, &PropertyWidget::onEditorValueChanged);
        editor = dateEdit;
        break;
    }
    case PropertyValueType::Time:
    {
        QTimeEdit *timeEdit = new QTimeEdit(parent);
        timeEdit->setTime(value->value().toTime());
        connect(timeEdit, &QTimeEdit::timeChanged, this, &PropertyWidget::onEditorValueChanged);
        editor = timeEdit;
        break;
    }
    case PropertyValueType::DateTime:
    {
        QDateTimeEdit *dateTimeEdit = new QDateTimeEdit(parent);
        dateTimeEdit->setDateTime(value->value().toDateTime());
        connect(dateTimeEdit, &QDateTimeEdit::dateTimeChanged, this, &PropertyWidget::onEditorValueChanged);
        editor = dateTimeEdit;
        break;
    }
    case PropertyValueType::Color:
    {
        QPushButton *colorButton = new QPushButton(parent);
        QColor color = value->value().value<QColor>();
        colorButton->setStyleSheet(QString("background-color: %1; color: %2;").arg(
            color.name(), 
            (color.lightness() < 128) ? "white" : "black"
        ));
        colorButton->setText(color.name());
        connect(colorButton, &QPushButton::clicked, this, &PropertyWidget::onColorButtonClicked);
        editor = colorButton;
        break;
    }
    case PropertyValueType::List:
        editor = createListEditor(value, parent);
        break;
    case PropertyValueType::Map:
        editor = createMapEditor(value, parent);
        break;
    default:
        // 对于其他类型，使用只读的文本显示
        QLineEdit *lineEdit = new QLineEdit(parent);
        lineEdit->setText(value->value().toString());
        lineEdit->setReadOnly(true);
        editor = lineEdit;
        break;
    }
    
    if (editor) {
        editor->setEnabled(!value->isReadOnly());
        m_editorToPropertyMap[editor] = value;
    }
    
    return editor;
}

QWidget *PropertyWidget::createListEditor(QPropertyValue *value, QWidget *parent)
{
    if (!value) {
        return nullptr;
    }
    
    QWidget *editorWidget = new QWidget(parent);
    QVBoxLayout *layout = new QVBoxLayout(editorWidget);
    
    QLabel *label = new QLabel(tr("列表大小: %1").arg(value->listSize()), editorWidget);
    QPushButton *editButton = new QPushButton(tr("编辑列表"), editorWidget);
    
    layout->addWidget(label);
    layout->addWidget(editButton);
    layout->addStretch();
    
    // 连接编辑按钮信号
    connect(editButton, &QPushButton::clicked, [this, value]() {
        // 这里应该打开一个列表编辑对话框
        QMessageBox::information(this, tr("列表编辑"), tr("此处应该打开列表编辑对话框"));
    });
    
    return editorWidget;
}

QWidget *PropertyWidget::createMapEditor(QPropertyValue *value, QWidget *parent)
{
    if (!value) {
        return nullptr;
    }
    
    QWidget *editorWidget = new QWidget(parent);
    QVBoxLayout *layout = new QVBoxLayout(editorWidget);
    
    QLabel *label = new QLabel(tr("映射大小: %1").arg(value->mapSize()), editorWidget);
    QPushButton *editButton = new QPushButton(tr("编辑映射"), editorWidget);
    
    layout->addWidget(label);
    layout->addWidget(editButton);
    layout->addStretch();
    
    // 连接编辑按钮信号
    connect(editButton, &QPushButton::clicked, [this, value]() {
        // 这里应该打开一个映射编辑对话框
        QMessageBox::information(this, tr("映射编辑"), tr("此处应该打开映射编辑对话框"));
    });
    
    return editorWidget;
}

void PropertyWidget::buildPropertyTree(QTreeWidgetItem *parentItem, QPropertyObject *object, int level)
{
    if (!parentItem || !object) {
        return;
    }
    
    // 存储对象指针到树节点
    parentItem->setData(0, Qt::UserRole, QVariant::fromValue(object));
    
    // 按类型分组
    QMap<QString, QTreeWidgetItem *> typeGroups;
    
    // 添加属性
    const auto &properties = object->properties();
    for (QPropertyValue *value : properties) {
        if (value && (m_showReadOnlyProperties || !value->isReadOnly())) {
            if (m_groupByType) {
                QString typeName = getPropertyTypeDisplayName(value->type());
                if (!typeGroups.contains(typeName)) {
                    QTreeWidgetItem *groupItem = new QTreeWidgetItem(parentItem);
                    groupItem->setText(0, typeName);
                    groupItem->setFont(0, QFont("Arial", 9, QFont::Bold));
                    typeGroups[typeName] = groupItem;
                }
                addPropertyToTree(typeGroups[typeName], value);
            } else {
                addPropertyToTree(parentItem, value);
            }
        }
    }
    
    // 添加子对象
    const auto &childObjects = object->childObjects();
    for (QPropertyObject *child : childObjects) {
        if (child) {
            QTreeWidgetItem *childItem = new QTreeWidgetItem(parentItem);
            childItem->setText(0, child->name());
            childItem->setText(2, "Object");
            setObjectItemIcon(childItem);
            
            // 存储子对象指针
            childItem->setData(0, Qt::UserRole, QVariant::fromValue(child));
            
            // 如果是第一层子对象，预先展开
            if (level <= 2) {
                buildPropertyTree(childItem, child, level + 1);
                childItem->setExpanded(true);
            }
        }
    }
}

void PropertyWidget::addPropertyToTree(QTreeWidgetItem *parentItem, QPropertyValue *value)
{
    if (!parentItem || !value) {
        return;
    }
    
    QTreeWidgetItem *item = new QTreeWidgetItem(parentItem);
    item->setText(0, getPropertyDisplayName(value));
    item->setText(1, value->value().toString());
    item->setText(2, getPropertyTypeDisplayName(value->type()));
    
    // 设置图标
    setItemIcon(item, value->type());
    
    // 存储属性指针
    item->setData(0, Qt::UserRole + 1, QVariant::fromValue(value));
    
    // 如果是只读属性，设置不同的颜色
    if (value->isReadOnly()) {
        QColor readOnlyColor(128, 128, 128);
        for (int i = 0; i < 3; ++i) {
            item->setForeground(i, readOnlyColor);
        }
    }
}

void PropertyWidget::updatePropertyEditor(QTreeWidgetItem *item)
{
    if (!item) {
        clearPropertyEditor();
        return;
    }
    
    // 清空现有编辑器
    clearPropertyEditor();
    
    // 检查是否是属性项
    if (item->data(0, Qt::UserRole + 1).canConvert<QPropertyValue*>()) {
        QPropertyValue *value = item->data(0, Qt::UserRole + 1).value<QPropertyValue*>();
        if (value) {
            // 创建属性信息组
            QGroupBox *propertyGroup = new QGroupBox(tr("属性信息"), m_propertyEditorWidget);
            QFormLayout *formLayout = new QFormLayout(propertyGroup);
            
            // 添加属性名称
            formLayout->addRow(tr("名称:"), new QLabel(value->name()));
            
            // 添加属性类型
            formLayout->addRow(tr("类型:"), new QLabel(getPropertyTypeDisplayName(value->type())));
            
            // 添加值编辑器
            QWidget *editor = createEditorForProperty(value, propertyGroup);
            if (editor) {
                formLayout->addRow(tr("值:"), editor);
            }
            
            // 添加单位（如果有）
            if (!value->unit().isEmpty()) {
                formLayout->addRow(tr("单位:"), new QLabel(value->unit()));
            }
            
            // 添加范围（如果有）
            if (value->minValue().isValid()) {
                formLayout->addRow(tr("最小值:"), new QLabel(value->minValue().toString()));
            }
            if (value->maxValue().isValid()) {
                formLayout->addRow(tr("最大值:"), new QLabel(value->maxValue().toString()));
            }
            
            // 添加精度（如果适用）
            if (value->precision() > 0) {
                formLayout->addRow(tr("精度:"), new QLabel(QString::number(value->precision())));
            }
            
            // 添加元数据信息
            if (m_showAdvancedProperties && !value->metadata().isEmpty()) {
                QGroupBox *metadataGroup = new QGroupBox(tr("元数据"), m_propertyEditorWidget);
                QFormLayout *metaLayout = new QFormLayout(metadataGroup);
                
                for (auto it = value->metadata().constBegin(); it != value->metadata().constEnd(); ++it) {
                    metaLayout->addRow(it.key() + ":", new QLabel(it.value().toString()));
                }
                
                m_editorLayout->addWidget(metadataGroup);
            }
            
            m_editorLayout->addWidget(propertyGroup);
        }
    } 
    // 检查是否是对象项
    else if (item->data(0, Qt::UserRole).canConvert<QPropertyObject*>()) {
        QPropertyObject *object = item->data(0, Qt::UserRole).value<QPropertyObject*>();
        if (object) {
            // 创建对象信息组
            QGroupBox *objectGroup = new QGroupBox(tr("对象信息"), m_propertyEditorWidget);
            QFormLayout *formLayout = new QFormLayout(objectGroup);
            
            formLayout->addRow(tr("对象名称:"), new QLabel(object->name()));
            formLayout->addRow(tr("属性数量:"), new QLabel(QString::number(object->properties().size())));
            formLayout->addRow(tr("子对象数量:"), new QLabel(QString::number(object->childObjects().size())));
            
            m_editorLayout->addWidget(objectGroup);
        }
    }
    
    m_editorLayout->addStretch();
}

void PropertyWidget::clearPropertyEditor()
{
    // 清空编辑器布局
    QLayoutItem *item;
    while ((item = m_editorLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            m_editorToPropertyMap.remove(item->widget());
            delete item->widget();
        }
        delete item;
    }
    
    m_editorToPropertyMap.clear();
}

QString PropertyWidget::getPropertyDisplayName(QPropertyValue *value)
{
    if (!value) {
        return QString();
    }
    
    // 优先使用displayName，如果没有则使用name
    if (!value->displayName().isEmpty()) {
        return value->displayName();
    }
    
    return value->name();
}

QString PropertyWidget::getPropertyTypeDisplayName(PropertyValueType type)
{
    switch (type) {
    case PropertyValueType::Invalid: return tr("无效");
    case PropertyValueType::Bool: return tr("布尔值");
    case PropertyValueType::Int: return tr("整数");
    case PropertyValueType::Float: return tr("浮点数");
    case PropertyValueType::Double: return tr("双精度浮点数");
    case PropertyValueType::Char: return tr("字符");
    case PropertyValueType::String: return tr("字符串");
    case PropertyValueType::Enum: return tr("枚举");
    case PropertyValueType::DateTime: return tr("日期时间");
    case PropertyValueType::Date: return tr("日期");
    case PropertyValueType::Time: return tr("时间");
    case PropertyValueType::ByteArray: return tr("字节数组");
    case PropertyValueType::Color: return tr("颜色");
    case PropertyValueType::Size: return tr("大小");
    case PropertyValueType::Point: return tr("点");
    case PropertyValueType::Rect: return tr("矩形");
    case PropertyValueType::Line: return tr("线条");
    case PropertyValueType::List: return tr("列表");
    case PropertyValueType::Map: return tr("映射");
    default: return tr("未知");
    }
}

void PropertyWidget::setItemIcon(QTreeWidgetItem *item, PropertyValueType type)
{
    // 这里可以设置不同类型的图标
    // 简化处理，只设置基本颜色
    QColor iconColor;
    
    switch (type) {
    case PropertyValueType::Bool: iconColor = Qt::blue;
        break;
    case PropertyValueType::Int: iconColor = Qt::darkGreen;
        break;
    case PropertyValueType::Float:
    case PropertyValueType::Double: iconColor = Qt::green;
        break;
    case PropertyValueType::String:
    case PropertyValueType::Char: iconColor = Qt::darkRed;
        break;
    case PropertyValueType::Enum: iconColor = Qt::purple;
        break;
    case PropertyValueType::DateTime:
    case PropertyValueType::Date:
    case PropertyValueType::Time: iconColor = Qt::darkYellow;
        break;
    case PropertyValueType::Color: iconColor = Qt::red;
        break;
    case PropertyValueType::List: iconColor = Qt::darkBlue;
        break;
    case PropertyValueType::Map: iconColor = Qt::darkGray;
        break;
    default: iconColor = Qt::gray;
        break;
    }
    
    // 创建简单的颜色图标
    QPixmap pixmap(16, 16);
    pixmap.fill(iconColor);
    item->setIcon(0, QIcon(pixmap));
}

void PropertyWidget::setObjectItemIcon(QTreeWidgetItem *item)
{
    // 设置对象类型的图标
    QPixmap pixmap(16, 16);
    pixmap.fill(QColor(100, 100, 200));
    item->setIcon(0, QIcon(pixmap));
}

void PropertyWidget::saveCurrentState()
{
    // 保存当前状态
    if (!m_propertyObject) {
        m_lastState.clear();
        return;
    }
    
    m_lastState = getCurrentState();
}

void PropertyWidget::restoreState()
{
    // 恢复状态
    // 实际应用中可能需要更复杂的恢复逻辑
}

QMap<QString, QVariant> PropertyWidget::getCurrentState() const
{
    // 获取当前状态，用于检测变化
    QMap<QString, QVariant> state;
    
    if (!m_propertyObject) {
        return state;
    }
    
    // 收集所有属性值
    const auto &properties = m_propertyObject->properties();
    for (QPropertyValue *value : properties) {
        if (value) {
            state[value->name()] = value->value();
        }
    }
    
    return state;
}

void PropertyWidget::updateEditorValue(QWidget *editor, const QVariant &value)
{
    if (!editor) {
        return;
    }
    
    if (QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor)) {
        lineEdit->setText(value.toString());
    } else if (QCheckBox *checkBox = qobject_cast<QCheckBox *>(editor)) {
        checkBox->setChecked(value.toBool());
    } else if (QSpinBox *spinBox = qobject_cast<QSpinBox *>(editor)) {
        spinBox->setValue(value.toInt());
    } else if (QDoubleSpinBox *doubleSpinBox = qobject_cast<QDoubleSpinBox *>(editor)) {
        doubleSpinBox->setValue(value.toDouble());
    } else if (QComboBox *comboBox = qobject_cast<QComboBox *>(editor)) {
        comboBox->setCurrentText(value.toString());
    } else if (QDateEdit *dateEdit = qobject_cast<QDateEdit *>(editor)) {
        dateEdit->setDate(value.toDate());
    } else if (QTimeEdit *timeEdit = qobject_cast<QTimeEdit *>(editor)) {
        timeEdit->setTime(value.toTime());
    } else if (QDateTimeEdit *dateTimeEdit = qobject_cast<QDateTimeEdit *>(editor)) {
        dateTimeEdit->setDateTime(value.toDateTime());
    } else if (QPushButton *colorButton = qobject_cast<QPushButton *>(editor)) {
        QColor color = value.value<QColor>();
        colorButton->setStyleSheet(QString("background-color: %1; color: %2;").arg(
            color.name(), 
            (color.lightness() < 128) ? "white" : "black"
        ));
        colorButton->setText(color.name());
    }
}

void PropertyWidget::updateTreeItemValue(QPropertyValue *value)
{
    // 在树中查找对应的项并更新值
    // 简化实现，实际应用中可能需要更高效的查找方法
    QTreeWidgetItemIterator it(m_treeWidget);
    while (*it) {
        QTreeWidgetItem *item = *it;
        if (item->data(0, Qt::UserRole + 1).canConvert<QPropertyValue*>()) {
            QPropertyValue *itemValue = item->data(0, Qt::UserRole + 1).value<QPropertyValue*>();
            if (itemValue == value) {
                item->setText(1, value->value().toString());
                break;
            }
        }
        ++it;
    }
}