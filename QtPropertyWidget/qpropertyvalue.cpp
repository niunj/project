#include "qpropertyvalue.h"

QPropertyValue::QPropertyValue(const QString &name, const QVariant &value, QObject *parent)
    : QObject(parent)
    , m_name(name)
    , m_value(value)
    , m_defaultValue(value)
{
    updateTypeFromValue(value);
    allocateResources();
}

QPropertyValue::QPropertyValue(const QString &name, PropertyValueType type, const QVariant &value, QObject *parent)
    : QObject(parent)
    , m_name(name)
    , m_value(value)
    , m_defaultValue(value)
    , m_type(type)
{
    allocateResources();
}

QPropertyValue::~QPropertyValue()
{
    aboutToBeDestroyed();
    deallocateResources();
}

QString QPropertyValue::name() const
{
    if (!canReadInternal())
        return QString();
    return m_name;
}

void QPropertyValue::setName(const QString &name)
{
    if (m_name != name && canWriteInternal()) {
        m_name = name;
        emit nameChanged(name);
    }
}

QVariant QPropertyValue::value() const
{
    if (!canReadInternal())
        return QVariant();
    return m_value;
}

void QPropertyValue::setValue(const QVariant &value)
{
    if (m_value == value || !canWriteInternal() || m_locked) 
        return;
    
    const QVariant &oldValue = m_value;
    
    // 应用变更过滤器
    if (m_changeFilter && !m_changeFilter(value))
        return;
    
    // 应用约束
    QVariant constrainedValue = applyConstraints(value);
    
    // 验证值
    if (!validate(constrainedValue)) {
        emit validationFailed("Validation failed");
        return;
    }
    
    // 触发beforeValueChanged信号
    emit beforeValueChanged(oldValue, constrainedValue);
    
    // 记录历史
    recordHistory("setValue");
    
    // 更新值
    m_value = constrainedValue;
    m_modified = true;
    
    // 更新缓存
    updateCache();
    
    // 更新状态
    updateStatus();
    
    // 触发值变更信号
    emit valueChanged(m_value);
    emit afterValueChanged(m_value);
    emit modifiedChanged(m_modified);
    
    // 更新依赖项
    onValueChanged(oldValue, m_value);
    
    // 更新计算属性
    if (m_computed)
        updateComputedInternal();
    
    // 更新绑定目标
    for (auto target : m_boundTargets) {
        if (target && target != this)
            target->onBoundSourceChanged(m_value);
    }
}

QString QPropertyValue::unit() const
{
    return m_unit;
}

void QPropertyValue::setUnit(const QString &unit)
{
    if (m_unit != unit) {
        m_unit = unit;
        emit unitChanged(unit);
    }
}

QVariant QPropertyValue::minValue() const
{
    return m_minValue;
}

void QPropertyValue::setMinValue(const QVariant &minValue)
{
    if (m_minValue != minValue) {
        m_minValue = minValue;
        emit minValueChanged(minValue);
    }
}

QVariant QPropertyValue::maxValue() const
{
    return m_maxValue;
}

void QPropertyValue::setMaxValue(const QVariant &maxValue)
{
    if (m_maxValue != maxValue) {
        m_maxValue = maxValue;
        emit maxValueChanged(maxValue);
    }
}

int QPropertyValue::precision() const
{
    return m_precision;
}

void QPropertyValue::setPrecision(int precision)
{
    if (m_precision != precision) {
        m_precision = precision;
        emit precisionChanged(precision);
    }
}

PropertyValueType QPropertyValue::type() const
{
    return m_type;
}

void QPropertyValue::setType(PropertyValueType type)
{
    if (m_type != type) {
        m_type = type;
        emit typeChanged(type);
    }
}

bool QPropertyValue::isReadOnly() const
{
    return m_readOnly || hasStatusFlag(StatusFlag::ReadOnly);
}

void QPropertyValue::setReadOnly(bool readOnly)
{
    if (m_readOnly != readOnly) {
        m_readOnly = readOnly;
        if (readOnly) {
            addStatusFlag(StatusFlag::ReadOnly);
        } else {
            removeStatusFlag(StatusFlag::ReadOnly);
        }
        emit readOnlyChanged(readOnly);
        emit statusChanged(m_statusFlags);
    }
}

bool QPropertyValue::isRequired() const
{
    return m_required || hasValidationRule(ValidationRule::Required);
}

void QPropertyValue::setRequired(bool required)
{
    if (m_required != required) {
        m_required = required;
        if (required) {
            addValidationRule(ValidationRule::Required);
        } else {
            removeValidationRule(ValidationRule::Required);
        }
        emit requiredChanged(required);
        emit validationRulesChanged(m_validationRules);
    }
}

QVariant QPropertyValue::defaultValue() const
{
    return m_defaultValue;
}

void QPropertyValue::setDefaultValue(const QVariant &defaultValue)
{
    if (m_defaultValue != defaultValue) {
        m_defaultValue = defaultValue;
        emit defaultValueChanged(defaultValue);
    }
}

bool QPropertyValue::isModified() const
{
    return m_modified || hasStatusFlag(StatusFlag::Modified);
}

void QPropertyValue::setModified(bool modified)
{
    if (m_modified != modified) {
        m_modified = modified;
        if (modified) {
            addStatusFlag(StatusFlag::Modified);
        } else {
            removeStatusFlag(StatusFlag::Modified);
        }
        emit modifiedChanged(modified);
        emit statusChanged(m_statusFlags);
    }
}

void QPropertyValue::resetToDefault()
{
    setValue(m_defaultValue);
}

void QPropertyValue::clear()
{
    setValue(QVariant());
}

QString QPropertyValue::toString() const
{
    return m_value.toString();
}

bool QPropertyValue::toBool() const
{
    return m_value.toBool();
}

int QPropertyValue::toInt() const
{
    return m_value.toInt();
}

float QPropertyValue::toFloat() const
{
    return m_value.toFloat();
}

double QPropertyValue::toDouble() const
{
    return m_value.toDouble();
}

void QPropertyValue::updateTypeFromValue(const QVariant &value)
{
    m_type = variantTypeToPropertyType(value.type());
}

bool QPropertyValue::canConvertTo(PropertyValueType type) const
{
    // 实现类型转换检查
    switch (type) {
        case PropertyValueType::Int:
            return m_value.canConvert<int>();
        case PropertyValueType::Float:
            return m_value.canConvert<float>();
        case PropertyValueType::Double:
            return m_value.canConvert<double>();
        case PropertyValueType::Bool:
            return m_value.canConvert<bool>();
        case PropertyValueType::String:
            return m_value.canConvert<QString>();
        case PropertyValueType::Char:
            return m_value.canConvert<char>();
        default:
            return false;
    }
}

QVariant QPropertyValue::convertTo(PropertyValueType type) const
{
    if (!canConvertTo(type))
        return QVariant();
    
    switch (type) {
        case PropertyValueType::Int:
            return m_value.toInt();
        case PropertyValueType::Float:
            return m_value.toFloat();
        case PropertyValueType::Double:
            return m_value.toDouble();
        case PropertyValueType::Bool:
            return m_value.toBool();
        case PropertyValueType::String:
            return m_value.toString();
        case PropertyValueType::Char:
            return m_value.toChar();
        default:
            return QVariant();
    }
}

// 列表操作方法
QVariantList QPropertyValue::listValue() const
{
    if (m_type == PropertyValueType::List)
        return m_value.toList();
    return QVariantList();
}

void QPropertyValue::setListValue(const QVariantList &list)
{
    if (m_type == PropertyValueType::List) {
        setValue(list);
    }
}

void QPropertyValue::addListElement(const QVariant &element)
{
    if (m_type == PropertyValueType::List) {
        QVariantList list = m_value.toList();
        list.append(element);
        setValue(list);
    }
}

void QPropertyValue::removeListElement(int index)
{
    if (m_type == PropertyValueType::List) {
        QVariantList list = m_value.toList();
        if (index >= 0 && index < list.size()) {
            list.removeAt(index);
            setValue(list);
        }
    }
}

QVariant QPropertyValue::getListElement(int index) const
{
    if (m_type == PropertyValueType::List) {
        const QVariantList &list = m_value.toList();
        if (index >= 0 && index < list.size())
            return list.at(index);
    }
    return QVariant();
}

void QPropertyValue::setListElement(int index, const QVariant &element)
{
    if (m_type == PropertyValueType::List) {
        QVariantList list = m_value.toList();
        if (index >= 0 && index < list.size()) {
            list[index] = element;
            setValue(list);
        }
    }
}

int QPropertyValue::listSize() const
{
    if (m_type == PropertyValueType::List)
        return m_value.toList().size();
    return 0;
}

// 映射操作方法
QVariantMap QPropertyValue::mapValue() const
{
    if (m_type == PropertyValueType::Map)
        return m_value.toMap();
    return QVariantMap();
}

void QPropertyValue::setMapValue(const QVariantMap &map)
{
    if (m_type == PropertyValueType::Map) {
        setValue(map);
    }
}

void QPropertyValue::setMapEntry(const QString &key, const QVariant &value)
{
    if (m_type == PropertyValueType::Map) {
        QVariantMap map = m_value.toMap();
        map[key] = value;
        setValue(map);
    }
}

QVariant QPropertyValue::getMapEntry(const QString &key) const
{
    if (m_type == PropertyValueType::Map) {
        const QVariantMap &map = m_value.toMap();
        return map.value(key);
    }
    return QVariant();
}

void QPropertyValue::removeMapEntry(const QString &key)
{
    if (m_type == PropertyValueType::Map) {
        QVariantMap map = m_value.toMap();
        map.remove(key);
        setValue(map);
    }
}

bool QPropertyValue::hasMapEntry(const QString &key) const
{
    if (m_type == PropertyValueType::Map) {
        const QVariantMap &map = m_value.toMap();
        return map.contains(key);
    }
    return false;
}

QStringList QPropertyValue::mapKeys() const
{
    if (m_type == PropertyValueType::Map) {
        return m_value.toMap().keys();
    }
    return QStringList();
}

int QPropertyValue::mapSize() const
{
    if (m_type == PropertyValueType::Map)
        return m_value.toMap().size();
    return 0;
}

// 数据绑定支持
void QPropertyValue::bindTo(QPropertyValue *source)
{
    if (!source || source == this) 
        return;
    
    // 移除现有绑定
    if (m_boundSource) {
        disconnect(m_boundSource, &QPropertyValue::valueChanged, this, &QPropertyValue::onBoundSourceChanged);
        m_boundSource->m_boundTargets.removeOne(this);
    }
    
    // 设置新绑定
    m_boundSource = source;
    source->m_boundTargets.append(this);
    
    // 连接信号
    connect(source, &QPropertyValue::valueChanged, this, &QPropertyValue::onBoundSourceChanged);
    
    // 初始同步值
    onBoundSourceChanged(source->value());
    
    emit boundChanged(true);
}

void QPropertyValue::unbind()
{
    if (m_boundSource) {
        disconnect(m_boundSource, &QPropertyValue::valueChanged, this, &QPropertyValue::onBoundSourceChanged);
        m_boundSource->m_boundTargets.removeOne(this);
        m_boundSource = nullptr;
        emit boundChanged(false);
    }
}

bool QPropertyValue::isBound() const
{
    return m_boundSource != nullptr;
}

// 历史记录支持
void QPropertyValue::undo()
{
    if (!m_historyEnabled || m_history.isEmpty() || m_historyIndex <= 0) 
        return;
    
    m_historyIndex--;
    const HistoryEntry &entry = m_history.at(m_historyIndex);
    m_value = entry.value;
    
    emit valueChanged(m_value);
    emit historyChanged(m_historyIndex > 0, m_historyIndex < m_history.size() - 1);
}

void QPropertyValue::redo()
{
    if (!m_historyEnabled || m_history.isEmpty() || m_historyIndex >= m_history.size() - 1) 
        return;
    
    m_historyIndex++;
    const HistoryEntry &entry = m_history.at(m_historyIndex);
    m_value = entry.value;
    
    emit valueChanged(m_value);
    emit historyChanged(m_historyIndex > 0, m_historyIndex < m_history.size() - 1);
}

bool QPropertyValue::canUndo() const
{
    return m_historyEnabled && !m_history.isEmpty() && m_historyIndex > 0;
}

bool QPropertyValue::canRedo() const
{
    return m_historyEnabled && !m_history.isEmpty() && m_historyIndex < m_history.size() - 1;
}

void QPropertyValue::setHistoryEnabled(bool enabled)
{
    m_historyEnabled = enabled;
    if (!enabled) {
        m_history.clear();
        m_historyIndex = -1;
    }
    emit historyEnabledChanged(enabled);
}

bool QPropertyValue::isHistoryEnabled() const
{
    return m_historyEnabled;
}

// 自定义数据存储
void QPropertyValue::setUserData(const QVariant &data)
{
    m_userData = data;
}

QVariant QPropertyValue::userData() const
{
    return m_userData;
}

// 工厂方法实现
QPropertyValue *QPropertyValue::create(const QString &name, PropertyValueType type, const QVariant &value)
{
    return new QPropertyValue(name, type, value);
}

QPropertyValue *QPropertyValue::createInt(const QString &name, int value)
{
    return new QPropertyValue(name, PropertyValueType::Int, value);
}

QPropertyValue *QPropertyValue::createFloat(const QString &name, float value)
{
    return new QPropertyValue(name, PropertyValueType::Float, value);
}

QPropertyValue *QPropertyValue::createDouble(const QString &name, double value)
{
    return new QPropertyValue(name, PropertyValueType::Double, value);
}

QPropertyValue *QPropertyValue::createBool(const QString &name, bool value)
{
    return new QPropertyValue(name, PropertyValueType::Bool, value);
}

QPropertyValue *QPropertyValue::createString(const QString &name, const QString &value)
{
    return new QPropertyValue(name, PropertyValueType::String, value);
}

QPropertyValue *QPropertyValue::createChar(const QString &name, char value)
{
    return new QPropertyValue(name, PropertyValueType::Char, value);
}

QPropertyValue *QPropertyValue::createList(const QString &name, const QVariantList &value)
{
    return new QPropertyValue(name, PropertyValueType::List, value);
}

QPropertyValue *QPropertyValue::createMap(const QString &name, const QVariantMap &value)
{
    return new QPropertyValue(name, PropertyValueType::Map, value);
}

// 静态工具方法实现
PropertyValueType QPropertyValue::variantTypeToPropertyType(const QVariant::Type &variantType)
{
    switch (variantType) {
        case QVariant::Bool:
            return PropertyValueType::Bool;
        case QVariant::Int:
        case QVariant::LongLong:
        case QVariant::UInt:
        case QVariant::ULongLong:
            return PropertyValueType::Int;
        case QVariant::Float:
            return PropertyValueType::Float;
        case QVariant::Double:
            return PropertyValueType::Double;
        case QVariant::Char:
            return PropertyValueType::Char;
        case QVariant::String:
        case QVariant::ByteArray:
            return PropertyValueType::String;
        case QVariant::List:
            return PropertyValueType::List;
        case QVariant::Map:
        case QVariant::Hash:
            return PropertyValueType::Map;
        default:
            return PropertyValueType::Invalid;
    }
}

QVariant::Type QPropertyValue::propertyTypeToVariantType(PropertyValueType propertyType)
{
    switch (propertyType) {
        case PropertyValueType::Bool:
            return QVariant::Bool;
        case PropertyValueType::Int:
            return QVariant::Int;
        case PropertyValueType::Float:
            return QVariant::Float;
        case PropertyValueType::Double:
            return QVariant::Double;
        case PropertyValueType::Char:
            return QVariant::Char;
        case PropertyValueType::String:
            return QVariant::String;
        case PropertyValueType::List:
            return QVariant::List;
        case PropertyValueType::Map:
            return QVariant::Map;
        default:
            return QVariant::Invalid;
    }
}

bool QPropertyValue::isBasicType(PropertyValueType type)
{
    switch (type) {
        case PropertyValueType::Bool:
        case PropertyValueType::Int:
        case PropertyValueType::Float:
        case PropertyValueType::Double:
        case PropertyValueType::Char:
        case PropertyValueType::String:
            return true;
        default:
            return false;
    }
}

bool QPropertyValue::isContainerType(PropertyValueType type)
{
    return type == PropertyValueType::List || type == PropertyValueType::Map;
}

QPropertyValue *QPropertyValue::createFromVariant(const QString &name, const QVariant &variant, QObject *parent)
{
    PropertyValueType type = variantTypeToPropertyType(variant.type());
    return new QPropertyValue(name, type, variant, parent);
}

// 静态批量操作方法
void QPropertyValue::batchUpdate(QList<QPropertyValue *> properties, const std::function<void()> &updateFunc)
{
    // 开始批量更新
    for (auto prop : properties) {
        if (prop)
            prop->beginBatchUpdate();
    }
    
    // 执行更新函数
    if (updateFunc)
        updateFunc();
    
    // 结束批量更新
    for (auto prop : properties) {
        if (prop)
            prop->endBatchUpdate();
    }
}

void QPropertyValue::validateAll(QList<QPropertyValue *> properties)
{
    for (auto prop : properties) {
        if (prop)
            prop->validate();
    }
}

void QPropertyValue::resetAllToDefault(QList<QPropertyValue *> properties)
{
    batchUpdate(properties, [&properties]() {
        for (auto prop : properties) {
            if (prop)
                prop->resetToDefault();
        }
    });
}

// 保护方法实现
bool QPropertyValue::validate(const QVariant &value) const
{
    // 自定义验证器优先
    if (m_validator)
        return m_validator(value);
    
    // 应用验证规则
    if (m_required && value.isNull())
        return false;
    
    // 范围验证
    if (m_type == PropertyValueType::Int || m_type == PropertyValueType::Float || m_type == PropertyValueType::Double) {
        if (m_minValue.isValid() && value < m_minValue)
            return false;
        if (m_maxValue.isValid() && value > m_maxValue)
            return false;
    }
    
    return true;
}

QVariant QPropertyValue::applyConstraints(const QVariant &value) const
{
    if (m_constraint)
        return m_constraint(value);
    return value;
}

void QPropertyValue::onValueChanged(const QVariant &oldValue, const QVariant &newValue)
{
    // 通知依赖项
    for (auto dep : m_dependencies) {
        if (dep && dep != this)
            dep->onDependencyChanged(newValue);
    }
}

void QPropertyValue::onDependencyChanged(const QVariant &value)
{
    // 更新计算属性
    if (m_computed)
        updateComputedInternal();
}

void QPropertyValue::onBoundSourceChanged(const QVariant &value)
{
    // 不记录历史，因为这是绑定源的变更
    bool historyEnabled = m_historyEnabled;
    m_historyEnabled = false;
    setValue(value);
    m_historyEnabled = historyEnabled;
}

void QPropertyValue::recordHistory(const QString &operation)
{
    if (!m_historyEnabled)
        return;
    
    // 如果在历史中间，移除后面的历史记录
    if (m_historyIndex < m_history.size() - 1) {
        m_history = m_history.mid(0, m_historyIndex + 1);
    }
    
    // 添加新记录
    HistoryEntry entry;
    entry.value = m_value;
    entry.operation = operation;
    m_history.append(entry);
    m_historyIndex = m_history.size() - 1;
    
    // 限制历史记录大小
    if (m_history.size() > m_historyLimit) {
        m_history.removeFirst();
        m_historyIndex--;
    }
    
    emit historyChanged(canUndo(), canRedo());
}

void QPropertyValue::allocateResources()
{
    setupLocking();
    setupThreading();
    setupTimers();
}

void QPropertyValue::deallocateResources()
{
    cleanupTimers();
    cleanupThreading();
    cleanupLocking();
}

void QPropertyValue::setupLocking()
{
    if (m_threadSafe && !m_mutex) {
        m_mutex = new QMutex();
    }
}

void QPropertyValue::cleanupLocking()
{
    delete m_mutex;
    m_mutex = nullptr;
}

void QPropertyValue::setupThreading()
{
    // 线程相关设置
}

void QPropertyValue::cleanupThreading()
{
    // 线程清理
}

void QPropertyValue::setupTimers()
{
    if (m_updateThrottling > 0 && !m_updateTimer) {
        m_updateTimer = new QTimer(this);
        // 定时器设置
    }
}

void QPropertyValue::cleanupTimers()
{
    delete m_updateTimer;
    m_updateTimer = nullptr;
}

void QPropertyValue::aboutToBeDestroyed()
{
    // 解除所有绑定
    unbind();
    for (auto target : m_boundTargets) {
        if (target) {
            target->unbind();
        }
    }
}

// 状态和验证规则管理
StatusFlags QPropertyValue::status() const
{
    return m_statusFlags;
}

void QPropertyValue::addStatusFlag(StatusFlag flag)
{
    m_statusFlags |= flag;
}

void QPropertyValue::removeStatusFlag(StatusFlag flag)
{
    m_statusFlags &= ~flag;
}

bool QPropertyValue::hasStatusFlag(StatusFlag flag) const
{
    return m_statusFlags & flag;
}

ValidationRules QPropertyValue::validationRules() const
{
    return m_validationRules;
}

void QPropertyValue::addValidationRule(ValidationRule rule)
{
    m_validationRules |= rule;
}

void QPropertyValue::removeValidationRule(ValidationRule rule)
{
    m_validationRules &= ~rule;
}

bool QPropertyValue::hasValidationRule(ValidationRule rule) const
{
    return m_validationRules & rule;
}

// 批量更新
void QPropertyValue::beginBatchUpdate()
{
    m_batchUpdating = true;
}

void QPropertyValue::endBatchUpdate()
{
    m_batchUpdating = false;
    processBatchUpdate();
}

void QPropertyValue::processBatchUpdate()
{
    // 批量更新处理
}

// 依赖管理
void QPropertyValue::addDependency(QPropertyValue *dependency)
{
    if (dependency && !m_dependencies.contains(dependency)) {
        m_dependencies.append(dependency);
        connect(dependency, &QPropertyValue::valueChanged, this, &QPropertyValue::onDependencyChanged);
    }
}

void QPropertyValue::removeDependency(QPropertyValue *dependency)
{
    if (dependency) {
        m_dependencies.removeOne(dependency);
        disconnect(dependency, &QPropertyValue::valueChanged, this, &QPropertyValue::onDependencyChanged);
    }
}

QList<QPropertyValue *> QPropertyValue::dependencies() const
{
    return m_dependencies;
}

// 计算属性
void QPropertyValue::setComputedFunction(const std::function<QVariant()> &function)
{
    m_computedFunction = function;
    m_computed = true;
    updateComputedInternal();
}

void QPropertyValue::updateComputedInternal()
{
    if (m_computedFunction) {
        QVariant computedValue = m_computedFunction();
        // 不记录历史
        bool historyEnabled = m_historyEnabled;
        m_historyEnabled = false;
        m_value = computedValue;
        m_historyEnabled = historyEnabled;
        emit valueChanged(m_value);
    }
}

void QPropertyValue::updateComputed()
{
    updateComputedInternal();
}

// 元数据管理
QVariantMap QPropertyValue::metadata() const
{
    return m_metadata;
}

void QPropertyValue::setMetadata(const QVariantMap &metadata)
{
    m_metadata = metadata;
    emit metadataChanged(metadata);
}

void QPropertyValue::setMetadataValue(const QString &key, const QVariant &value)
{
    m_metadata[key] = value;
    emit metadataChanged(m_metadata);
}

QVariant QPropertyValue::getMetadataValue(const QString &key) const
{
    return m_metadata.value(key);
}

bool QPropertyValue::hasMetadataValue(const QString &key) const
{
    return m_metadata.contains(key);
}

void QPropertyValue::removeMetadataValue(const QString &key)
{
    m_metadata.remove(key);
    emit metadataChanged(m_metadata);
}

// 访问控制
void QPropertyValue::setAccessControl(const std::function<bool()> &canRead, const std::function<bool()> &canWrite)
{
    m_canReadFunction = canRead;
    m_canWriteFunction = canWrite;
}

bool QPropertyValue::canReadInternal() const
{
    if (m_canReadFunction)
        return m_canReadFunction();
    return true;
}

bool QPropertyValue::canWriteInternal() const
{
    if (m_readOnly || hasStatusFlag(StatusFlag::ReadOnly))
        return false;
    if (m_canWriteFunction)
        return m_canWriteFunction();
    return true;
}