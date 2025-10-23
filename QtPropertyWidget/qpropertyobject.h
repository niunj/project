#pragma once

#include <QObject>
#include <QVariant>
#include <QList>
#include <QString>
#include <QXmlStreamWriter>
#include <QXmlStreamReader>
#include "qpropertyvalue.h"

/**
 * @brief 属性类型枚举
 */
enum class PropertyType
{
    Invalid = 0,
    Bool,
    Int,
    Float,
    Double,
    String,
    Enum,
    Object,
    Array,
    Map
};

/**
 * @brief 属性对象类，继承自QObject
 * 用于表示具有各种特性的属性，支持嵌套子属性
 */
class QPropertyObject : public QObject
{
    Q_OBJECT
    
    // Qt属性系统声明
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QVariant value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(QString unit READ unit WRITE setUnit NOTIFY unitChanged)
    Q_PROPERTY(QVariant minValue READ minValue WRITE setMinValue NOTIFY minValueChanged)
    Q_PROPERTY(QVariant maxValue READ maxValue WRITE setMaxValue NOTIFY maxValueChanged)
    Q_PROPERTY(int precision READ precision WRITE setPrecision NOTIFY precisionChanged)
    Q_PROPERTY(PropertyType type READ type WRITE setType NOTIFY typeChanged)
    
public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit QPropertyObject(QObject *parent = nullptr);
    
    /**
     * @brief 带参构造函数
     * @param name 属性名称
     * @param value 属性值
     * @param parent 父对象
     */
    QPropertyObject(const QString &name, const QVariant &value = QVariant(), QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~QPropertyObject();
    
    // Getter和Setter方法
    QString name() const;
    void setName(const QString &name);
    
    QVariant value() const;
    void setValue(const QVariant &value);
    
    QString unit() const;
    void setUnit(const QString &unit);
    
    QVariant minValue() const;
    void setMinValue(const QVariant &minValue);
    
    QVariant maxValue() const;
    void setMaxValue(const QVariant &maxValue);
    
    int precision() const;
    void setPrecision(int precision);
    
    PropertyType type() const;
    void setType(PropertyType type);
    
    // 属性管理
    QPropertyValue *addProperty(const QString &name, PropertyValueType type, const QVariant &value = QVariant());
    QPropertyValue *addProperty(const QString &name, const QVariant &value = QVariant());
    QPropertyValue *property(const QString &name) const;
    bool hasProperty(const QString &name) const;
    void removeProperty(const QString &name);
    QList<QPropertyValue *> properties() const;
    
    // 子属性对象管理
    QPropertyObject *addChildObject(const QString &name);
    QPropertyObject *childObject(const QString &name) const;
    bool hasChildObject(const QString &name) const;
    void removeChildObject(const QString &name);
    QList<QPropertyObject *> childObjects() const;
    
    // 数组操作（使用子对象表示数组项）
    QList<QPropertyObject *> arrayItems() const;
    QPropertyObject *addArrayItem(const QString &name = QString());
    void removeArrayItem(const QString &name);
    QPropertyObject *getArrayItem(int index) const;
    void setArrayItem(int index, QPropertyObject *item);
    int arraySize() const;
    
    // 映射操作（使用QPropertyValue的Map类型）
    void setMapProperty(const QString &name, const QVariantMap &map);
    QVariantMap getMapProperty(const QString &name) const;
    void setMapItem(const QString &name, const QString &key, const QVariant &value);
    QVariant getMapItem(const QString &name, const QString &key) const;
    void removeMapItem(const QString &name, const QString &key);
    
    // 列表属性操作（使用QPropertyValue的List类型）
    void setListProperty(const QString &name, const QVariantList &list);
    QVariantList getListProperty(const QString &name) const;
    void addListElement(const QString &name, const QVariant &element);
    void removeListElement(const QString &name, int index);
    QVariant getListElement(const QString &name, int index) const;
    void setListElement(const QString &name, int index, const QVariant &element);
    int listSize(const QString &name) const;
    
    // 嵌套结构操作
    QPropertyObject *createNestedStructure(const QString &path);
    QPropertyValue *addNestedProperty(const QString &path, PropertyValueType type, const QVariant &value = QVariant());
    
    // 批量属性创建
    QPropertyValue *addIntProperty(const QString &name, int value = 0);
    QPropertyValue *addFloatProperty(const QString &name, float value = 0.0f);
    QPropertyValue *addDoubleProperty(const QString &name, double value = 0.0);
    QPropertyValue *addBoolProperty(const QString &name, bool value = false);
    QPropertyValue *addStringProperty(const QString &name, const QString &value = QString());
    QPropertyValue *addListProperty(const QString &name, const QVariantList &value = QVariantList());
    QPropertyValue *addMapProperty(const QString &name, const QVariantMap &value = QVariantMap());
    
    // 操作符重载
    QPropertyObject &operator=(const QPropertyObject &other);
    bool operator==(const QPropertyObject &other) const;
    bool operator!=(const QPropertyObject &other) const;
    
    // 拷贝功能
    QPropertyObject *clone() const;
    void copyFrom(const QPropertyObject &other);
    
    // 序列化
    QJsonObject toJsonObject() const;
    bool fromJsonObject(const QJsonObject &json);
    QByteArray toJson() const;
    bool fromJson(const QByteArray &data);
    
    // XML序列化
    bool saveToXml(QXmlStreamWriter *writer) const;
    bool loadFromXml(QXmlStreamReader *reader);
    bool saveToXmlFile(const QString &fileName) const;
    bool loadFromXmlFile(const QString &fileName);
    
    // 二进制序列化
    QByteArray serializeToBinary() const;
    bool deserializeFromBinary(const QByteArray &data);
    bool saveToBinaryFile(const QString &fileName) const;
    bool loadFromBinaryFile(const QString &fileName);
    
    // 嵌套路径访问
    QPropertyObject *getChildByPath(const QString &path);
    QPropertyValue *getPropertyByPath(const QString &path);
    QVariant getValueByPath(const QString &path);
    bool setValueByPath(const QString &path, const QVariant &value);
    
signals:
    // 属性变化信号
    void nameChanged(const QString &name);
    void valueChanged(const QVariant &value);
    void unitChanged(const QString &unit);
    void minValueChanged(const QVariant &minValue);
    void maxValueChanged(const QVariant &maxValue);
    void precisionChanged(int precision);
    void typeChanged(PropertyType type);
    void childAdded(QPropertyObject *child);
    void childRemoved(QPropertyObject *child);
    
protected:
    // 验证属性值是否在范围内
    bool validateValue(const QVariant &value) const;
    
    // 将QVariant转换为对应的属性类型
    void updateTypeFromValue(const QVariant &value);
    
    // 结构体转换相关方法
    template<typename T>
    static QPropertyObject *fromStruct(const T &structObj, const QString &name = QString());
    
    template<typename T>
    bool toStruct(T &structObj) const;
    
    // 数组操作方法
    bool isArray() const;
    QList<QPropertyObject *> arrayItems() const;
    QPropertyObject *addArrayItem(const QVariant &value = QVariant());
    void removeArrayItem(int index);

    // 映射操作方法
    bool isMap() const;
    QMap<QString, QPropertyObject *> mapItems() const;
    QPropertyObject *setMapItem(const QString &key, const QVariant &value = QVariant());
    QPropertyObject *getMapItem(const QString &key) const;
    void removeMapItem(const QString &key);
    
    // 内部辅助方法
    void setupArray();
    void setupMap();
    
private:
    QString m_name;              // 属性名称
    QVariant m_value;            // 属性值
    QString m_unit;              // 属性单位
    QVariant m_minValue;         // 最小值
    QVariant m_maxValue;         // 最大值
    int m_precision = 2;         // 精度（用于浮点数）
    PropertyType m_type = PropertyType::Invalid; // 属性类型
    
    // 存储属性列表
    QMap<QString, QPropertyValue *> m_properties;
    
    // 存储子属性对象列表
    QMap<QString, QPropertyObject *> m_childObjects;
    
    // 批量操作标志
    bool m_batchOperationInProgress = false;
    
    // 事务相关
    QList<QVariantMap> m_transactionHistory;
    int m_transactionIndex = -1;
    bool m_transactionInProgress = false;
    
    // 内部辅助方法
    void setupArray();
    void setupMap();
};