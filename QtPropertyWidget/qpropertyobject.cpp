#include "qpropertyobject.h"
#include "qpropertyvalue.h"
#include <QDataStream>
#include <QFile>
#include <QDebug>
#include <algorithm>

QPropertyObject::QPropertyObject(QObject *parent) : QObject(parent)
    , m_type(PropertyType::Object)
    , m_precision(2)
    , m_batchOperationInProgress(false)
    , m_transactionInProgress(false)
    , m_transactionIndex(-1)
{}

QPropertyObject::QPropertyObject(const QString &name, QObject *parent) : QObject(parent)
    , m_name(name)
    , m_type(PropertyType::Object)
    , m_precision(2)
    , m_batchOperationInProgress(false)
    , m_transactionInProgress(false)
    , m_transactionIndex(-1)
{}

QPropertyObject::~QPropertyObject()
{
    // 清理属性
    qDeleteAll(m_properties);
    m_properties.clear();
    
    // 清理子对象
    qDeleteAll(m_childObjects);
    m_childObjects.clear();
}

// 基本属性getter/setter方法
QString QPropertyObject::name() const
{
    return m_name;
}

void QPropertyObject::setName(const QString &name)
{
    if (m_name != name) {
        m_name = name;
        emit nameChanged(name);
    }
}

QVariant QPropertyObject::value() const
{
    return m_value;
}

void QPropertyObject::setValue(const QVariant &value)
{
    if (m_value != value) {
        m_value = value;
        emit valueChanged(value);
    }
}

QString QPropertyObject::unit() const
{
    return m_unit;
}

void QPropertyObject::setUnit(const QString &unit)
{
    if (m_unit != unit) {
        m_unit = unit;
        emit unitChanged(unit);
    }
}

QVariant QPropertyObject::minValue() const
{
    return m_minValue;
}

void QPropertyObject::setMinValue(const QVariant &minValue)
{
    if (m_minValue != minValue) {
        m_minValue = minValue;
        emit minValueChanged(minValue);
    }
}

QVariant QPropertyObject::maxValue() const
{
    return m_maxValue;
}

void QPropertyObject::setMaxValue(const QVariant &maxValue)
{
    if (m_maxValue != maxValue) {
        m_maxValue = maxValue;
        emit maxValueChanged(maxValue);
    }
}

int QPropertyObject::precision() const
{
    return m_precision;
}

void QPropertyObject::setPrecision(int precision)
{
    if (m_precision != precision) {
        m_precision = precision;
        emit precisionChanged(precision);
    }
}

PropertyType QPropertyObject::type() const
{
    return m_type;
}

void QPropertyObject::setType(PropertyType type)
{
    if (m_type != type) {
        m_type = type;
        emit typeChanged(type);
    }
}

// 属性管理方法
QPropertyValue *QPropertyObject::addProperty(const QString &name, PropertyValueType type, const QVariant &value)
{
    // 如果属性已存在，返回现有属性
    if (m_properties.contains(name)) {
        QPropertyValue *prop = m_properties.value(name);
        prop->setType(type);
        prop->setValue(value);
        return prop;
    }
    
    // 创建新属性
    QPropertyValue *newProp = new QPropertyValue(name, type, value, this);
    m_properties[name] = newProp;
    
    // 连接信号
    connect(newProp, &QPropertyValue::valueChanged, this, &QPropertyObject::onPropertyValueChanged);
    
    emit propertyAdded(name, newProp);
    return newProp;
}

QPropertyValue *QPropertyObject::addProperty(const QString &name, const QVariant &value)
{
    PropertyValueType type = QPropertyValue::variantTypeToPropertyType(value.type());
    return addProperty(name, type, value);
}

QPropertyValue *QPropertyObject::property(const QString &name) const
{
    return m_properties.value(name, nullptr);
}

bool QPropertyObject::hasProperty(const QString &name) const
{
    return m_properties.contains(name);
}

void QPropertyObject::removeProperty(const QString &name)
{
    if (m_properties.contains(name)) {
        QPropertyValue *prop = m_properties.take(name);
        emit propertyRemoved(name, prop);
        delete prop;
    }
}

QList<QPropertyValue *> QPropertyObject::properties() const
{
    return m_properties.values();
}

// 子属性对象管理方法
QPropertyObject *QPropertyObject::addChildObject(const QString &name)
{
    // 如果子对象已存在，返回现有对象
    if (m_childObjects.contains(name)) {
        return m_childObjects.value(name);
    }
    
    // 创建新子对象
    QPropertyObject *newChild = new QPropertyObject(name, this);
    m_childObjects[name] = newChild;
    
    // 连接信号
    connect(newChild, &QPropertyObject::valueChanged, this, &QPropertyObject::onChildObjectChanged);
    
    emit childObjectAdded(name, newChild);
    return newChild;
}

QPropertyObject *QPropertyObject::childObject(const QString &name) const
{
    return m_childObjects.value(name, nullptr);
}

bool QPropertyObject::hasChildObject(const QString &name) const
{
    return m_childObjects.contains(name);
}

void QPropertyObject::removeChildObject(const QString &name)
{
    if (m_childObjects.contains(name)) {
        QPropertyObject *child = m_childObjects.take(name);
        emit childObjectRemoved(name, child);
        delete child;
    }
}

QList<QPropertyObject *> QPropertyObject::childObjects() const
{
    return m_childObjects.values();
}

// 批量属性创建方法
QPropertyValue *QPropertyObject::addIntProperty(const QString &name, int value)
{
    return addProperty(name, PropertyValueType::Int, value);
}

QPropertyValue *QPropertyObject::addFloatProperty(const QString &name, float value)
{
    return addProperty(name, PropertyValueType::Float, value);
}

QPropertyValue *QPropertyObject::addDoubleProperty(const QString &name, double value)
{
    return addProperty(name, PropertyValueType::Double, value);
}

QPropertyValue *QPropertyObject::addBoolProperty(const QString &name, bool value)
{
    return addProperty(name, PropertyValueType::Bool, value);
}

QPropertyValue *QPropertyObject::addStringProperty(const QString &name, const QString &value)
{
    return addProperty(name, PropertyValueType::String, value);
}

QPropertyValue *QPropertyObject::addListProperty(const QString &name, const QVariantList &value)
{
    return addProperty(name, PropertyValueType::List, value);
}

QPropertyValue *QPropertyObject::addMapProperty(const QString &name, const QVariantMap &value)
{
    return addProperty(name, PropertyValueType::Map, value);
}

// 数组操作方法
QList<QPropertyObject *> QPropertyObject::arrayItems() const
{
    // 过滤子对象，找出以数组格式命名的对象（如"[0]", "[1]"等）
    QList<QPropertyObject *> array;    
    foreach (const QString &key, m_childObjects.keys()) {
        if (key.startsWith('[') && key.endsWith(']')) {
            array.append(m_childObjects.value(key));
        }
    }
    
    // 按索引排序
    std::sort(array.begin(), array.end(), [](QPropertyObject *a, QPropertyObject *b) {
        int indexA = a->name().mid(1, a->name().length() - 2).toInt();
        int indexB = b->name().mid(1, b->name().length() - 2).toInt();
        return indexA < indexB;
    });
    
    return array;
}

QPropertyObject *QPropertyObject::addArrayItem(const QString &name)
{
    QString itemName;
    if (name.isEmpty()) {
        // 自动生成数组索引名称
        int index = arraySize();
        itemName = QString("[%1]").arg(index);
    } else {
        itemName = name;
    }
    
    return addChildObject(itemName);
}

void QPropertyObject::removeArrayItem(const QString &name)
{
    removeChildObject(name);
    
    // 重新索引数组项
    QList<QPropertyObject *> items = arrayItems();
    for (int i = 0; i < items.size(); i++) {
        QPropertyObject *item = items[i];
        QString newName = QString("[%1]").arg(i);
        if (item->name() != newName) {
            m_childObjects.remove(item->name());
            item->setName(newName);
            m_childObjects[newName] = item;
        }
    }
}

QPropertyObject *QPropertyObject::getArrayItem(int index) const
{
    QString key = QString("[%1]").arg(index);
    return m_childObjects.value(key, nullptr);
}

void QPropertyObject::setArrayItem(int index, QPropertyObject *item)
{
    if (!item) return;
    
    QString key = QString("[%1]").arg(index);
    
    // 移除旧项
    if (m_childObjects.contains(key)) {
        delete m_childObjects.take(key);
    }
    
    // 设置新项
    item->setParent(this);
    item->setName(key);
    m_childObjects[key] = item;
    
    connect(item, &QPropertyObject::valueChanged, this, &QPropertyObject::onChildObjectChanged);
}

int QPropertyObject::arraySize() const
{
    return arrayItems().size();
}

// 映射操作方法
void QPropertyObject::setMapProperty(const QString &name, const QVariantMap &map)
{
    QPropertyValue *prop = property(name);
    if (!prop) {
        prop = addMapProperty(name, map);
    } else {
        prop->setMapValue(map);
    }
}

QVariantMap QPropertyObject::getMapProperty(const QString &name) const
{
    QPropertyValue *prop = property(name);
    if (prop && prop->type() == PropertyValueType::Map) {
        return prop->mapValue();
    }
    return QVariantMap();
}

void QPropertyObject::setMapItem(const QString &name, const QString &key, const QVariant &value)
{
    QPropertyValue *prop = property(name);
    if (!prop) {
        prop = addMapProperty(name);
    }
    prop->setMapEntry(key, value);
}

QVariant QPropertyObject::getMapItem(const QString &name, const QString &key) const
{
    QPropertyValue *prop = property(name);
    if (prop && prop->type() == PropertyValueType::Map) {
        return prop->getMapEntry(key);
    }
    return QVariant();
}

void QPropertyObject::removeMapItem(const QString &name, const QString &key)
{
    QPropertyValue *prop = property(name);
    if (prop && prop->type() == PropertyValueType::Map) {
        prop->removeMapEntry(key);
    }
}

// 列表属性操作方法
void QPropertyObject::setListProperty(const QString &name, const QVariantList &list)
{
    QPropertyValue *prop = property(name);
    if (!prop) {
        prop = addListProperty(name, list);
    } else {
        prop->setListValue(list);
    }
}

QVariantList QPropertyObject::getListProperty(const QString &name) const
{
    QPropertyValue *prop = property(name);
    if (prop && prop->type() == PropertyValueType::List) {
        return prop->listValue();
    }
    return QVariantList();
}

void QPropertyObject::addListElement(const QString &name, const QVariant &element)
{
    QPropertyValue *prop = property(name);
    if (!prop) {
        prop = addListProperty(name);
    }
    prop->addListElement(element);
}

void QPropertyObject::removeListElement(const QString &name, int index)
{
    QPropertyValue *prop = property(name);
    if (prop && prop->type() == PropertyValueType::List) {
        prop->removeListElement(index);
    }
}

QVariant QPropertyObject::getListElement(const QString &name, int index) const
{
    QPropertyValue *prop = property(name);
    if (prop && prop->type() == PropertyValueType::List) {
        return prop->getListElement(index);
    }
    return QVariant();
}

void QPropertyObject::setListElement(const QString &name, int index, const QVariant &element)
{
    QPropertyValue *prop = property(name);
    if (prop && prop->type() == PropertyValueType::List) {
        prop->setListElement(index, element);
    }
}

int QPropertyObject::listSize(const QString &name) const
{
    QPropertyValue *prop = property(name);
    if (prop && prop->type() == PropertyValueType::List) {
        return prop->listSize();
    }
    return 0;
}

// 嵌套结构操作方法
QPropertyObject *QPropertyObject::createNestedStructure(const QString &path)
{
    QStringList parts = path.split('.');
    QPropertyObject *current = this;
    
    foreach (const QString &part, parts) {
        if (part.isEmpty()) continue;
        
        QPropertyObject *next = current->childObject(part);
        if (!next) {
            next = current->addChildObject(part);
        }
        current = next;
    }
    
    return current;
}

QPropertyValue *QPropertyObject::addNestedProperty(const QString &path, PropertyValueType type, const QVariant &value)
{
    // 分离最后一个属性名和前面的路径
    int lastDotIndex = path.lastIndexOf('.');
    QString objectPath = (lastDotIndex >= 0) ? path.left(lastDotIndex) : "";
    QString propertyName = (lastDotIndex >= 0) ? path.mid(lastDotIndex + 1) : path;
    
    // 创建或获取目标对象
    QPropertyObject *targetObject = (objectPath.isEmpty()) ? this : createNestedStructure(objectPath);
    
    // 添加属性
    return targetObject->addProperty(propertyName, type, value);
}

// 信号处理槽
void QPropertyObject::onPropertyValueChanged(const QVariant &value)
{
    // 当属性值改变时触发对象值改变信号
    emit valueChanged(m_value);
    emit objectChanged(this);
}

void QPropertyObject::onChildObjectChanged(const QVariant &value)
{
    // 当子对象值改变时触发父对象改变信号
    emit objectChanged(this);
}

// 嵌套路径访问方法
QPropertyObject *QPropertyObject::getChildByPath(const QString &path)
{
    QStringList parts = path.split('.');
    QPropertyObject *current = this;
    
    foreach (const QString &part, parts) {
        if (part.isEmpty()) continue;
        
        QPropertyObject *next = current->childObject(part);
        if (!next) {
            return nullptr;
        }
        current = next;
    }
    
    return current;
}

QPropertyValue *QPropertyObject::getPropertyByPath(const QString &path)
{
    // 分离最后一个属性名和前面的路径
    int lastDotIndex = path.lastIndexOf('.');
    QString objectPath = (lastDotIndex >= 0) ? path.left(lastDotIndex) : "";
    QString propertyName = (lastDotIndex >= 0) ? path.mid(lastDotIndex + 1) : path;
    
    // 获取目标对象
    QPropertyObject *targetObject = (objectPath.isEmpty()) ? this : getChildByPath(objectPath);
    if (!targetObject) {
        return nullptr;
    }
    
    // 获取属性
    return targetObject->property(propertyName);
}

QVariant QPropertyObject::getValueByPath(const QString &path)
{
    QPropertyValue *prop = getPropertyByPath(path);
    if (prop) {
        return prop->value();
    }
    return QVariant();
}

bool QPropertyObject::setValueByPath(const QString &path, const QVariant &value)
{
    QPropertyValue *prop = getPropertyByPath(path);
    if (prop) {
        prop->setValue(value);
        return true;
    }
    return false;
}

// 序列化方法
QJsonObject QPropertyObject::toJsonObject() const
{
    QJsonObject jsonObj;
    
    // 基本属性
    jsonObj["name"] = m_name;
    jsonObj["type"] = static_cast<int>(m_type);
    
    // 序列化属性列表
    QJsonObject propertiesObj;
    foreach (const QString &key, m_properties.keys()) {
        QPropertyValue *prop = m_properties.value(key);
        if (prop) {
            propertiesObj[key] = QJsonValue::fromVariant(prop->value());
        }
    }
    jsonObj["properties"] = propertiesObj;
    
    // 序列化子对象
    QJsonObject childrenObj;
    foreach (const QString &key, m_childObjects.keys()) {
        QPropertyObject *child = m_childObjects.value(key);
        if (child) {
            childrenObj[key] = child->toJsonObject();
        }
    }
    jsonObj["children"] = childrenObj;
    
    return jsonObj;
}

bool QPropertyObject::fromJsonObject(const QJsonObject &json)
{
    // 清空现有数据
    clear();
    
    // 读取基本属性
    if (json.contains("name")) {
        m_name = json["name"].toString();
    }
    
    if (json.contains("type")) {
        m_type = static_cast<PropertyType>(json["type"].toInt());
    }
    
    // 读取属性列表
    if (json.contains("properties") && json["properties"].isObject()) {
        QJsonObject propertiesObj = json["properties"].toObject();
        foreach (const QString &key, propertiesObj.keys()) {
            QJsonValue jsonVal = propertiesObj[key];
            addProperty(key, jsonVal.toVariant());
        }
    }
    
    // 读取子对象
    if (json.contains("children") && json["children"].isObject()) {
        QJsonObject childrenObj = json["children"].toObject();
        foreach (const QString &key, childrenObj.keys()) {
            if (childrenObj[key].isObject()) {
                QPropertyObject *child = addChildObject(key);
                child->fromJsonObject(childrenObj[key].toObject());
            }
        }
    }
    
    return true;
}

QByteArray QPropertyObject::toJson() const
{
    QJsonDocument doc(toJsonObject());
    return doc.toJson(QJsonDocument::Indented);
}

bool QPropertyObject::fromJson(const QByteArray &json)
{
    QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return false;
    }
    return fromJsonObject(doc.object());
}

// 清空方法
void QPropertyObject::clear()
{
    // 清理属性
    qDeleteAll(m_properties);
    m_properties.clear();
    
    // 清理子对象
    qDeleteAll(m_childObjects);
    m_childObjects.clear();
    
    emit cleared();
}

// 查找方法
QList<QPropertyObject *> QPropertyObject::findObjectsByName(const QString &name) const
{
    QList<QPropertyObject *> results;
    
    // 检查直接子对象
    foreach (QPropertyObject *child, m_childObjects.values()) {
        if (child->name() == name) {
            results.append(child);
        }
        
        // 递归查找子对象的子对象
        QList<QPropertyObject *> childResults = child->findObjectsByName(name);
        results.append(childResults);
    }
    
    return results;
}

QList<QPropertyValue *> QPropertyObject::findPropertiesByName(const QString &name) const
{
    QList<QPropertyValue *> results;
    
    // 检查直接属性
    foreach (QPropertyValue *prop, m_properties.values()) {
        if (prop->name() == name) {
            results.append(prop);
        }
    }
    
    // 递归查找子对象的属性
    foreach (QPropertyObject *child, m_childObjects.values()) {
        QList<QPropertyValue *> childResults = child->findPropertiesByName(name);
        results.append(childResults);
    }
    
    return results;
}

QList<QPropertyValue *> QPropertyObject::findPropertiesByType(PropertyValueType type) const
{
    QList<QPropertyValue *> results;
    
    // 检查直接属性
    foreach (QPropertyValue *prop, m_properties.values()) {
        if (prop->type() == type) {
            results.append(prop);
        }
    }
    
    // 递归查找子对象的属性
    foreach (QPropertyObject *child, m_childObjects.values()) {
        QList<QPropertyValue *> childResults = child->findPropertiesByType(type);
        results.append(childResults);
    }
    
    return results;
}

// 遍历方法
void QPropertyObject::forEachProperty(const std::function<void(QPropertyValue *)> &func)
{
    if (!func) return;
    
    foreach (QPropertyValue *prop, m_properties.values()) {
        func(prop);
    }
    
    // 递归遍历子对象的属性
    foreach (QPropertyObject *child, m_childObjects.values()) {
        child->forEachProperty(func);
    }
}

void QPropertyObject::forEachChild(const std::function<void(QPropertyObject *)> &func)
{
    if (!func) return;
    
    foreach (QPropertyObject *child, m_childObjects.values()) {
        func(child);
        // 递归遍历子对象的子对象
        child->forEachChild(func);
    }
}

// 过滤方法
QList<QPropertyValue *> QPropertyObject::filterProperties(const std::function<bool(QPropertyValue *)> &predicate) const
{
    QList<QPropertyValue *> results;
    
    if (!predicate) return results;
    
    foreach (QPropertyValue *prop, m_properties.values()) {
        if (predicate(prop)) {
            results.append(prop);
        }
    }
    
    // 递归过滤子对象的属性
    foreach (QPropertyObject *child, m_childObjects.values()) {
        QList<QPropertyValue *> childResults = child->filterProperties(predicate);
        results.append(childResults);
    }
    
    return results;
}

QList<QPropertyObject *> QPropertyObject::filterChildren(const std::function<bool(QPropertyObject *)> &predicate) const
{
    QList<QPropertyObject *> results;
    
    if (!predicate) return results;
    
    foreach (QPropertyObject *child, m_childObjects.values()) {
        if (predicate(child)) {
            results.append(child);
        }
        // 递归过滤子对象的子对象
        QList<QPropertyObject *> childResults = child->filterChildren(predicate);
        results.append(childResults);
    }
    
    return results;
}

// 批量操作方法
void QPropertyObject::beginBatchOperations()
{
    m_batchOperationInProgress = true;
}

void QPropertyObject::endBatchOperations()
{
    m_batchOperationInProgress = false;
    emit batchOperationsCompleted();
}

bool QPropertyObject::isBatchOperationInProgress() const
{
    return m_batchOperationInProgress;
}

// 内部辅助方法
QMap<QString, QPropertyObject *> QPropertyObject::childObjectsInternal() const
{
    return m_childObjects;
}

QMap<QString, QPropertyValue *> QPropertyObject::propertiesInternal() const
{
    return m_properties;
}

// 操作符重载
bool QPropertyObject::operator==(const QPropertyObject &other) const
{
    if (m_name != other.m_name ||
        m_value != other.m_value ||
        m_unit != other.m_unit ||
        m_minValue != other.m_minValue ||
        m_maxValue != other.m_maxValue ||
        m_precision != other.m_precision ||
        m_type != other.m_type ||
        m_children.size() != other.m_children.size()) {
        return false;
    }
    
    // 比较子属性
    for (int i = 0; i < m_children.size(); ++i) {
        QPropertyObject *thisChild = m_children[i];
        QPropertyObject *otherChild = other.m_children[i];
        if (!thisChild || !otherChild || *thisChild != *otherChild) {
            return false;
        }
    }
    
    return true;
}

bool QPropertyObject::operator!=(const QPropertyObject &other) const
{
    return !(*this == other);
}

QPropertyObject *QPropertyObject::clone() const
{
    QPropertyObject *cloneObj = new QPropertyObject(m_name, m_value);
    cloneObj->m_unit = m_unit;
    cloneObj->m_minValue = m_minValue;
    cloneObj->m_maxValue = m_maxValue;
    cloneObj->m_precision = m_precision;
    cloneObj->m_type = m_type;
    
    // 克隆子属性
    for (auto child : m_children) {
        if (child) {
            QPropertyObject *childClone = child->clone();
            cloneObj->addChild(childClone);
        }
    }
    
    return cloneObj;
}

void QPropertyObject::copyFrom(const QPropertyObject &other)
{
    // 先清理当前子属性
    qDeleteAll(m_children);
    m_children.clear();
    
    // 复制基本属性
    m_name = other.m_name;
    m_value = other.m_value;
    m_unit = other.m_unit;
    m_minValue = other.m_minValue;
    m_maxValue = other.m_maxValue;
    m_precision = other.m_precision;
    m_type = other.m_type;
    
    // 复制子属性
    for (auto child : other.m_children) {
        if (child) {
            QPropertyObject *childClone = child->clone();
            addChild(childClone);
        }
    }
    
    // 发送信号通知变化
    emit nameChanged(m_name);
    emit valueChanged(m_value);
    emit unitChanged(m_unit);
    emit minValueChanged(m_minValue);
    emit maxValueChanged(m_maxValue);
    emit precisionChanged(m_precision);
    emit typeChanged(m_type);
}

bool QPropertyObject::saveToXml(QXmlStreamWriter *writer) const
{
    if (!writer) {
        return false;
    }
    
    writer->writeStartElement("Property");
    writer->writeAttribute("name", m_name);
    writer->writeAttribute("type", QString::number(static_cast<int>(m_type)));
    
    if (!m_value.isNull()) {
        writer->writeTextElement("Value", m_value.toString());
    }
    
    if (!m_unit.isEmpty()) {
        writer->writeTextElement("Unit", m_unit);
    }
    
    if (!m_minValue.isNull()) {
        writer->writeTextElement("MinValue", m_minValue.toString());
    }
    
    if (!m_maxValue.isNull()) {
        writer->writeTextElement("MaxValue", m_maxValue.toString());
    }
    
    writer->writeTextElement("Precision", QString::number(m_precision));
    
    // 保存子属性
    if (!m_children.isEmpty()) {
        writer->writeStartElement("Children");
        for (auto child : m_children) {
            if (child) {
                child->saveToXml(writer);
            }
        }
        writer->writeEndElement();
    }
    
    writer->writeEndElement();
    return true;
}

bool QPropertyObject::loadFromXml(QXmlStreamReader *reader)
{
    if (!reader || reader->name() != "Property") {
        return false;
    }
    
    // 读取属性
    QXmlStreamAttributes attrs = reader->attributes();
    if (attrs.hasAttribute("name")) {
        m_name = attrs.value("name").toString();
    }
    
    if (attrs.hasAttribute("type")) {
        m_type = static_cast<PropertyType>(attrs.value("type").toInt());
    }
    
    // 清空当前值和子属性
    m_value = QVariant();
    m_unit.clear();
    m_minValue = QVariant();
    m_maxValue = QVariant();
    m_precision = 2;
    
    qDeleteAll(m_children);
    m_children.clear();
    
    // 读取子元素
    while (reader->readNextStartElement()) {
        if (reader->name() == "Value") {
            m_value = reader->readElementText();
        } else if (reader->name() == "Unit") {
            m_unit = reader->readElementText();
        } else if (reader->name() == "MinValue") {
            m_minValue = reader->readElementText();
        } else if (reader->name() == "MaxValue") {
            m_maxValue = reader->readElementText();
        } else if (reader->name() == "Precision") {
            m_precision = reader->readElementText().toInt();
        } else if (reader->name() == "Children") {
            // 读取子属性
            while (reader->readNextStartElement()) {
                if (reader->name() == "Property") {
                    QPropertyObject *child = new QPropertyObject(this);
                    if (child->loadFromXml(reader)) {
                        m_children.append(child);
                        emit childAdded(child);
                    } else {
                        delete child;
                    }
                } else {
                    reader->skipCurrentElement();
                }
            }
        } else {
            reader->skipCurrentElement();
        }
    }
    
    // 更新类型
    if (!m_value.isNull()) {
        updateTypeFromValue(m_value);
    }
    
    return true;
}

bool QPropertyObject::saveToXmlFile(const QString &fileName) const
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to open file" << fileName << "for writing";
        return false;
    }
    
    QXmlStreamWriter writer(&file);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement("Properties");
    saveToXml(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    
    return file.flush();
}

bool QPropertyObject::loadFromXmlFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open file" << fileName << "for reading";
        return false;
    }
    
    QXmlStreamReader reader(&file);
    while (reader.readNextStartElement()) {
        if (reader.name() == "Properties") {
            while (reader.readNextStartElement()) {
                if (reader.name() == "Property") {
                    return loadFromXml(&reader);
                } else {
                    reader.skipCurrentElement();
                }
            }
        } else {
            reader.skipCurrentElement();
        }
    }
    
    return false;
}

QByteArray QPropertyObject::serializeToBinary() const
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    
    // 写入版本号
    stream << quint32(1);
    
    // 写入基本属性
    stream << m_name;
    stream << m_value;
    stream << m_unit;
    stream << m_minValue;
    stream << m_maxValue;
    stream << m_precision;
    stream << static_cast<quint32>(m_type);
    
    // 写入子属性数量
    stream << quint32(m_children.size());
    
    // 写入每个子属性
    for (auto child : m_children) {
        if (child) {
            QByteArray childData = child->serializeToBinary();
            stream << childData;
        } else {
            stream << QByteArray();
        }
    }
    
    return data;
}

bool QPropertyObject::deserializeFromBinary(const QByteArray &data)
{
    QDataStream stream(data);
    
    // 读取版本号
    quint32 version;
    stream >> version;
    
    if (version != 1) {
        qWarning() << "Unsupported version:" << version;
        return false;
    }
    
    // 清空当前数据
    m_name.clear();
    m_value = QVariant();
    m_unit.clear();
    m_minValue = QVariant();
    m_maxValue = QVariant();
    m_precision = 2;
    m_type = PropertyType::Invalid;
    
    qDeleteAll(m_children);
    m_children.clear();
    
    // 读取基本属性
    stream >> m_name;
    stream >> m_value;
    stream >> m_unit;
    stream >> m_minValue;
    stream >> m_maxValue;
    stream >> m_precision;
    
    quint32 typeInt;
    stream >> typeInt;
    m_type = static_cast<PropertyType>(typeInt);
    
    // 读取子属性数量
    quint32 childCount;
    stream >> childCount;
    
    // 读取每个子属性
    for (quint32 i = 0; i < childCount; ++i) {
        QByteArray childData;
        stream >> childData;
        
        if (!childData.isEmpty()) {
            QPropertyObject *child = new QPropertyObject(this);
            if (child->deserializeFromBinary(childData)) {
                m_children.append(child);
                emit childAdded(child);
            } else {
                delete child;
            }
        }
    }
    
    // 更新类型
    if (!m_value.isNull()) {
        updateTypeFromValue(m_value);
    }
    
    return !stream.atEnd();
}

bool QPropertyObject::saveToBinaryFile(const QString &fileName) const
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file" << fileName << "for writing";
        return false;
    }
    
    QByteArray data = serializeToBinary();
    qint64 bytesWritten = file.write(data);
    
    return bytesWritten == data.size() && file.flush();
}

bool QPropertyObject::loadFromBinaryFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file" << fileName << "for reading";
        return false;
    }
    
    QByteArray data = file.readAll();
    return deserializeFromBinary(data);
}

bool QPropertyObject::validateValue(const QVariant &value) const
{
    // 如果没有设置范围限制，则总是有效
    if (m_minValue.isNull() && m_maxValue.isNull()) {
        return true;
    }
    
    // 根据类型进行范围验证
    switch (m_type) {
    case PropertyType::Int:
    {
        bool ok;
        int val = value.toInt(&ok);
        if (!ok) return false;
        
        if (!m_minValue.isNull()) {
            int min = m_minValue.toInt();
            if (val < min) return false;
        }
        
        if (!m_maxValue.isNull()) {
            int max = m_maxValue.toInt();
            if (val > max) return false;
        }
        break;
    }
    case PropertyType::Double:
    {
        bool ok;
        double val = value.toDouble(&ok);
        if (!ok) return false;
        
        if (!m_minValue.isNull()) {
            double min = m_minValue.toDouble();
            if (val < min) return false;
        }
        
        if (!m_maxValue.isNull()) {
            double max = m_maxValue.toDouble();
            if (val > max) return false;
        }
        break;
    }
    // 其他类型暂不做范围验证
    default:
        return true;
    }
    
    return true;
}

void QPropertyObject::updateTypeFromValue(const QVariant &value)
{
    if (!value.isValid()) {
        setType(PropertyType::Invalid);
        return;
    }

    // 根据值的类型设置属性类型
    QVariant::Type type = value.type();
    
    // 检查是否为QPropertyObject*或其子类
    if (value.canConvert<QObject*>()) {
        QObject *obj = qvariant_cast<QObject*>(value);
        if (qobject_cast<QPropertyObject*>(obj)) {
            setType(PropertyType::Object);
            return;
        }
    }
    
    // 处理容器类型
    if (type == QVariant::List || type == QVariant::StringList || value.canConvert<QVariantList>()) {
        setType(PropertyType::Array);
        setupArray();
        return;
    }
    else if (type == QVariant::Map || type == QVariant::Hash || value.canConvert<QVariantMap>()) {
        setType(PropertyType::Map);
        setupMap();
        return;
    }
    
    // 处理基本类型
    if (type == QVariant::Int || type == QVariant::UInt || type == QVariant::LongLong || type == QVariant::ULongLong) {
        setType(PropertyType::Int);
    }
    else if (type == QVariant::Double) {
        setType(PropertyType::Double);
    }
    else if (value.canConvert<float>() || type == QVariant::Float) {
        setType(PropertyType::Float);
    }
    else if (type == QVariant::String) {
        setType(PropertyType::String);
    }
    else if (type == QVariant::Bool) {
        setType(PropertyType::Bool);
    }
    else {
        // 对于其他类型，尝试作为对象处理
        setType(PropertyType::Object);
    }
}

// 数组操作方法
bool QPropertyObject::isArray() const
{
    return m_type == PropertyType::Array;
}

QList<QPropertyObject *> QPropertyObject::arrayItems() const
{
    QList<QPropertyObject *> result;
    if (!isArray()) {
        return result;
    }
    return m_children;
}

QPropertyObject *QPropertyObject::addArrayItem(const QVariant &value)
{
    if (!isArray()) {
        setType(PropertyType::Array);
        setupArray();
    }
    
    QString name = QString("[%1]").arg(m_children.size());
    return addChild(name, value);
}

void QPropertyObject::removeArrayItem(int index)
{
    if (isArray() && index >= 0 && index < m_children.size()) {
        removeChild(m_children.at(index));
        
        // 重新索引剩余项
        for (int i = index; i < m_children.size(); ++i) {
            m_children[i]->setName(QString("[%1]").arg(i));
        }
    }
}

// 映射操作方法
bool QPropertyObject::isMap() const
{
    return m_type == PropertyType::Map;
}

QMap<QString, QPropertyObject *> QPropertyObject::mapItems() const
{
    QMap<QString, QPropertyObject *> result;
    if (!isMap()) {
        return result;
    }
    
    for (QPropertyObject *child : m_children) {
        if (child) {
            QString key = child->name();
            // 去掉@map:前缀（如果存在）
            if (key.startsWith("@map:")) {
                key = key.mid(5);
            }
            result.insert(key, child);
        }
    }
    
    return result;
}

QPropertyObject *QPropertyObject::setMapItem(const QString &key, const QVariant &value)
{
    if (!isMap()) {
        setType(PropertyType::Map);
        setupMap();
    }
    
    QString mapKey = "@map:" + key; // 添加特殊前缀避免冲突
    
    // 查找是否已存在
    QPropertyObject *child = findChild(mapKey);
    
    if (!child) {
        child = addChild(mapKey, value);
    } else {
        child->setValue(value);
    }
    
    return child;
}

QPropertyObject *QPropertyObject::getMapItem(const QString &key) const
{
    QString mapKey = "@map:" + key;
    return findChild(mapKey);
}

void QPropertyObject::removeMapItem(const QString &key)
{
    if (isMap()) {
        removeChild("@map:" + key);
    }
}

// 内部辅助方法
void QPropertyObject::setupArray()
{
    // 确保值是列表类型
    if (m_value.type() != QVariant::List) {
        m_value = QVariantList();
    }
    
    // 确保子属性索引连续
    for (int i = 0; i < m_children.size(); ++i) {
        m_children[i]->setName(QString("[%1]").arg(i));
    }
}

void QPropertyObject::setupMap()
{
    // 确保值是映射类型
    if (m_value.type() != QVariant::Map) {
        m_value = QVariantMap();
    }
}