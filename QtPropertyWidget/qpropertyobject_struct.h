#pragma once

#include "qpropertyobject.h"
#include <type_traits>
#include <string>
#include <vector>
#include <map>
#include <QMetaProperty>
#include <functional>

/**
 * @brief 属性元数据结构体，用于存储属性的额外信息
 */
struct PropertyMetadata {
    QString unit;             // 属性单位
    QVariant minValue;        // 最小值范围
    QVariant maxValue;        // 最大值范围
    int precision = 2;        // 精度（用于浮点数）
    
    // 默认构造函数
    PropertyMetadata() = default;
    
    // 构造函数
    PropertyMetadata(const QString &unit, const QVariant &minValue = QVariant(), 
                    const QVariant &maxValue = QVariant(), int precision = 2)
        : unit(unit), minValue(minValue), maxValue(maxValue), precision(precision) {}
};

/**
 * @brief 结构体属性描述器，用于在编译时或运行时描述结构体属性
 */
struct StructPropertyDescriptor {
    QString name;                    // 属性名称
    PropertyMetadata metadata;       // 属性元数据
    
    StructPropertyDescriptor(const QString &name, 
                            const PropertyMetadata &metadata = PropertyMetadata())
        : name(name), metadata(metadata) {}
};

/**
 * @brief 结构体描述器，用于描述整个结构体的信息
 */
template<typename T>
struct StructDescriptor {
    using PropertyMap = std::map<std::string, StructPropertyDescriptor>;
    PropertyMap properties;    // 属性映射
    
    // 添加属性描述
    StructDescriptor& add(const std::string &name, const PropertyMetadata &metadata = PropertyMetadata()) {
        properties[name] = StructPropertyDescriptor(QString::fromStdString(name), metadata);
        return *this;
    }
};

// 全局结构体描述器注册表
template<typename T>
struct StructDescriptorRegistry {
    static StructDescriptor<T> descriptor;
};

template<typename T>
StructDescriptor<T> StructDescriptorRegistry<T>::descriptor;

/**
 * @brief 结构体转换辅助函数，用于将结构体转换为QPropertyObject
 * @tparam T 结构体类型
 * @param structObj 结构体对象
 * @param name 属性名称
 * @return QPropertyObject指针
 */
template<typename T>
QPropertyObject *QPropertyObject::fromStruct(const T &structObj, const QString &name)
{
    QPropertyObject *obj = new QPropertyObject(name);
    
    // 使用C++反射技术来遍历结构体成员
    // 注意：这里需要根据具体结构体类型进行特化
    // 下面是一个通用实现，对于复杂结构体需要进行特化
    
    // 对于基本类型，直接设置值
    if constexpr (std::is_arithmetic_v<T>) {
        obj->setValue(QVariant::fromValue(structObj));
        
        // 应用元数据（如果有）
        auto &registry = StructDescriptorRegistry<T>::descriptor;
        if (!registry.properties.empty()) {
            // 对于基本类型，使用第一个属性的元数据
            auto it = registry.properties.begin();
            if (it != registry.properties.end()) {
                obj->setUnit(it->second.metadata.unit);
                obj->setMinValue(it->second.metadata.minValue);
                obj->setMaxValue(it->second.metadata.maxValue);
                obj->setPrecision(it->second.metadata.precision);
            }
        }
    }
    // 对于字符串类型
    else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, QString>) {
        obj->setValue(QString::fromStdString(structObj));
    }
    // 对于数组类型
    else if constexpr (std::is_array_v<T> || 
                      (std::is_same_v<typename std::remove_reference_t<T>, std::vector<int>> ||
                       std::is_same_v<typename std::remove_reference_t<T>, std::vector<float>> ||
                       std::is_same_v<typename std::remove_reference_t<T>, std::vector<double>> ||
                       std::is_same_v<typename std::remove_reference_t<T>, QVector<int>> ||
                       std::is_same_v<typename std::remove_reference_t<T>, QVector<float>> ||
                       std::is_same_v<typename std::remove_reference_t<T>, QVector<double>>)) {
        obj->setType(PropertyType::Array);
        obj->setupArray();
        
        // 应用数组的元数据
        auto &registry = StructDescriptorRegistry<T>::descriptor;
        auto it = registry.properties.find(name.toStdString());
        if (it != registry.properties.end()) {
            obj->setUnit(it->second.metadata.unit);
        }
        
        for (const auto &item : structObj) {
            QVariant var;
            if constexpr (std::is_same_v<typename std::remove_reference_t<decltype(item)>, int>) {
                var = item;
            } else if constexpr (std::is_same_v<typename std::remove_reference_t<decltype(item)>, float>) {
                var = item;
            } else if constexpr (std::is_same_v<typename std::remove_reference_t<decltype(item)>, double>) {
                var = item;
            }
            obj->addArrayItem(var);
        }
    }
    // 对于映射类型
    else if constexpr (std::is_same_v<typename std::remove_reference_t<T>, std::map<std::string, int>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, std::map<std::string, float>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, std::map<std::string, double>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, QMap<QString, int>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, QMap<QString, float>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, QMap<QString, double>>) {
        obj->setType(PropertyType::Map);
        obj->setupMap();
        
        // 应用映射的元数据
        auto &registry = StructDescriptorRegistry<T>::descriptor;
        auto it = registry.properties.find(name.toStdString());
        if (it != registry.properties.end()) {
            obj->setUnit(it->second.metadata.unit);
        }
        
        for (const auto &pair : structObj) {
            QString key;
            QVariant value;
            
            if constexpr (std::is_same_v<typename std::remove_reference_t<decltype(pair.first)>, std::string>) {
                key = QString::fromStdString(pair.first);
            } else {
                key = pair.first;
            }
            
            if constexpr (std::is_same_v<typename std::remove_reference_t<decltype(pair.second)>, int>) {
                value = pair.second;
            } else if constexpr (std::is_same_v<typename std::remove_reference_t<decltype(pair.second)>, float>) {
                value = pair.second;
            } else if constexpr (std::is_same_v<typename std::remove_reference_t<decltype(pair.second)>, double>) {
                value = pair.second;
            }
            
            QPropertyObject *mapItem = obj->setMapItem(key, value);
            
            // 应用映射项的元数据（如果有）
            auto &registry = StructDescriptorRegistry<T>::descriptor;
            auto itemIt = registry.properties.find(key.toStdString());
            if (itemIt != registry.properties.end()) {
                mapItem->setUnit(itemIt->second.metadata.unit);
                mapItem->setMinValue(itemIt->second.metadata.minValue);
                mapItem->setMaxValue(itemIt->second.metadata.maxValue);
                mapItem->setPrecision(itemIt->second.metadata.precision);
            }
        }
    }
    // 对于自定义结构体，需要手动映射
    else {
        obj->setType(PropertyType::Object);
        
        // 可以通过特化模板来处理具体的结构体类型
        // 这里默认不做任何处理
    }
    
    return obj;
}

/**
 * @brief 将QPropertyObject转换回结构体
 * @tparam T 结构体类型
 * @param structObj 结构体对象引用
 * @return 是否转换成功
 */
template<typename T>
bool QPropertyObject::toStruct(T &structObj) const
{
    // 使用C++反射技术来设置结构体成员
    // 注意：这里需要根据具体结构体类型进行特化
    
    // 对于基本类型
    if constexpr (std::is_arithmetic_v<T>) {
        if (m_value.canConvert<T>()) {
            structObj = m_value.value<T>();
            return true;
        }
    }
    // 对于字符串类型
    else if constexpr (std::is_same_v<T, std::string>) {
        if (m_value.canConvert<QString>()) {
            structObj = m_value.toString().toStdString();
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, QString>) {
        if (m_value.canConvert<QString>()) {
            structObj = m_value.toString();
            return true;
        }
    }
    // 对于数组类型
    else if constexpr (std::is_same_v<typename std::remove_reference_t<T>, std::vector<int>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, std::vector<float>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, std::vector<double>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, QVector<int>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, QVector<float>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, QVector<double>>) {
        if (!isArray()) {
            return false;
        }
        
        structObj.clear();
        for (auto item : m_children) {
            if (!item) continue;
            
            if constexpr (std::is_same_v<typename std::remove_reference_t<T>, std::vector<int>> ||
                         std::is_same_v<typename std::remove_reference_t<T>, QVector<int>>) {
                if (item->value().canConvert<int>()) {
                    structObj.push_back(item->value().toInt());
                }
            } else if constexpr (std::is_same_v<typename std::remove_reference_t<T>, std::vector<float>> ||
                               std::is_same_v<typename std::remove_reference_t<T>, QVector<float>>) {
                if (item->value().canConvert<float>()) {
                    structObj.push_back(item->value().toFloat());
                }
            } else if constexpr (std::is_same_v<typename std::remove_reference_t<T>, std::vector<double>> ||
                               std::is_same_v<typename std::remove_reference_t<T>, QVector<double>>) {
                if (item->value().canConvert<double>()) {
                    structObj.push_back(item->value().toDouble());
                }
            }
        }
        return true;
    }
    // 对于映射类型
    else if constexpr (std::is_same_v<typename std::remove_reference_t<T>, std::map<std::string, int>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, std::map<std::string, float>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, std::map<std::string, double>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, QMap<QString, int>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, QMap<QString, float>> ||
                      std::is_same_v<typename std::remove_reference_t<T>, QMap<QString, double>>) {
        if (!isMap()) {
            return false;
        }
        
        structObj.clear();
        auto mapItems = this->mapItems();
        
        for (auto it = mapItems.constBegin(); it != mapItems.constEnd(); ++it) {
            const QString &key = it.key();
            QPropertyObject *item = it.value();
            
            if (!item) continue;
            
            if constexpr (std::is_same_v<typename std::remove_reference_t<T>, std::map<std::string, int>> ||
                         std::is_same_v<typename std::remove_reference_t<T>, QMap<QString, int>>) {
                if (item->value().canConvert<int>()) {
                    if constexpr (std::is_same_v<typename std::remove_reference_t<T>, std::map<std::string, int>>) {
                        structObj[key.toStdString()] = item->value().toInt();
                    } else {
                        structObj[key] = item->value().toInt();
                    }
                }
            } else if constexpr (std::is_same_v<typename std::remove_reference_t<T>, std::map<std::string, float>> ||
                               std::is_same_v<typename std::remove_reference_t<T>, QMap<QString, float>>) {
                if (item->value().canConvert<float>()) {
                    if constexpr (std::is_same_v<typename std::remove_reference_t<T>, std::map<std::string, float>>) {
                        structObj[key.toStdString()] = item->value().toFloat();
                    } else {
                        structObj[key] = item->value().toFloat();
                    }
                }
            } else if constexpr (std::is_same_v<typename std::remove_reference_t<T>, std::map<std::string, double>> ||
                               std::is_same_v<typename std::remove_reference_t<T>, QMap<QString, double>>) {
                if (item->value().canConvert<double>()) {
                    if constexpr (std::is_same_v<typename std::remove_reference_t<T>, std::map<std::string, double>>) {
                        structObj[key.toStdString()] = item->value().toDouble();
                    } else {
                        structObj[key] = item->value().toDouble();
                    }
                }
            }
        }
        return true;
    }
    
    // 对于自定义结构体，需要手动映射
    qWarning() << "No specialization for struct type, conversion failed";
    return false;
}

/**
 * @brief 定义一个示例结构体，演示如何与QPropertyObject交互
 */
struct ExampleStruct {
    int intValue;
    float floatValue;
    double doubleValue;
    std::vector<int> intArray;
    std::vector<float> floatArray;
    std::vector<double> doubleArray;
    std::map<std::string, int> intMap;
    std::map<std::string, float> floatMap;
    std::map<std::string, double> doubleMap;
};

/**
 * @brief 特化ExampleStruct的fromStruct模板，支持元数据
 */
template<>
inline QPropertyObject *QPropertyObject::fromStruct<ExampleStruct>(const ExampleStruct &structObj, const QString &name)
{
    QPropertyObject *obj = new QPropertyObject(name);
    obj->setType(PropertyType::Object);
    
    // 获取结构体描述器
    auto &descriptor = StructDescriptorRegistry<ExampleStruct>::descriptor;
    
    // 添加基本类型属性
    QPropertyObject *intProp = obj->addChild("intValue", structObj.intValue);
    QPropertyObject *floatProp = obj->addChild("floatValue", structObj.floatValue);
    QPropertyObject *doubleProp = obj->addChild("doubleValue", structObj.doubleValue);
    
    // 应用元数据
    auto intIt = descriptor.properties.find("intValue");
    if (intIt != descriptor.properties.end()) {
        intProp->setUnit(intIt->second.metadata.unit);
        intProp->setMinValue(intIt->second.metadata.minValue);
        intProp->setMaxValue(intIt->second.metadata.maxValue);
    }
    
    auto floatIt = descriptor.properties.find("floatValue");
    if (floatIt != descriptor.properties.end()) {
        floatProp->setUnit(floatIt->second.metadata.unit);
        floatProp->setMinValue(floatIt->second.metadata.minValue);
        floatProp->setMaxValue(floatIt->second.metadata.maxValue);
        floatProp->setPrecision(floatIt->second.metadata.precision);
    }
    
    auto doubleIt = descriptor.properties.find("doubleValue");
    if (doubleIt != descriptor.properties.end()) {
        doubleProp->setUnit(doubleIt->second.metadata.unit);
        doubleProp->setMinValue(doubleIt->second.metadata.minValue);
        doubleProp->setMaxValue(doubleIt->second.metadata.maxValue);
        doubleProp->setPrecision(doubleIt->second.metadata.precision);
    }
    
    // 添加数组属性
    QPropertyObject *intArrayProp = obj->addChild("intArray");
    intArrayProp->setType(PropertyType::Array);
    intArrayProp->setupArray();
    for (int value : structObj.intArray) {
        intArrayProp->addArrayItem(value);
    }
    
    QPropertyObject *floatArrayProp = obj->addChild("floatArray");
    floatArrayProp->setType(PropertyType::Array);
    floatArrayProp->setupArray();
    for (float value : structObj.floatArray) {
        floatArrayProp->addArrayItem(value);
    }
    
    QPropertyObject *doubleArrayProp = obj->addChild("doubleArray");
    doubleArrayProp->setType(PropertyType::Array);
    doubleArrayProp->setupArray();
    for (double value : structObj.doubleArray) {
        doubleArrayProp->addArrayItem(value);
    }
    
    // 应用数组元数据
    auto intArrayIt = descriptor.properties.find("intArray");
    if (intArrayIt != descriptor.properties.end()) {
        intArrayProp->setUnit(intArrayIt->second.metadata.unit);
    }
    
    auto floatArrayIt = descriptor.properties.find("floatArray");
    if (floatArrayIt != descriptor.properties.end()) {
        floatArrayProp->setUnit(floatArrayIt->second.metadata.unit);
    }
    
    auto doubleArrayIt = descriptor.properties.find("doubleArray");
    if (doubleArrayIt != descriptor.properties.end()) {
        doubleArrayProp->setUnit(doubleArrayIt->second.metadata.unit);
    }
    
    // 添加映射属性
    QPropertyObject *intMapProp = obj->addChild("intMap");
    intMapProp->setType(PropertyType::Map);
    intMapProp->setupMap();
    for (const auto &pair : structObj.intMap) {
        intMapProp->setMapItem(QString::fromStdString(pair.first), pair.second);
    }
    
    QPropertyObject *floatMapProp = obj->addChild("floatMap");
    floatMapProp->setType(PropertyType::Map);
    floatMapProp->setupMap();
    for (const auto &pair : structObj.floatMap) {
        floatMapProp->setMapItem(QString::fromStdString(pair.first), pair.second);
    }
    
    QPropertyObject *doubleMapProp = obj->addChild("doubleMap");
    doubleMapProp->setType(PropertyType::Map);
    doubleMapProp->setupMap();
    for (const auto &pair : structObj.doubleMap) {
        doubleMapProp->setMapItem(QString::fromStdString(pair.first), pair.second);
    }
    
    // 应用映射元数据
    auto intMapIt = descriptor.properties.find("intMap");
    if (intMapIt != descriptor.properties.end()) {
        intMapProp->setUnit(intMapIt->second.metadata.unit);
    }
    
    auto floatMapIt = descriptor.properties.find("floatMap");
    if (floatMapIt != descriptor.properties.end()) {
        floatMapProp->setUnit(floatMapIt->second.metadata.unit);
    }
    
    auto doubleMapIt = descriptor.properties.find("doubleMap");
    if (doubleMapIt != descriptor.properties.end()) {
        doubleMapProp->setUnit(doubleMapIt->second.metadata.unit);
    }
    
    return obj;
}

/**
 * @brief 演示如何使用结构体描述器来注册和使用元数据
 */
inline void registerExampleStructDescriptor() {
    // 注册ExampleStruct的描述器，定义每个属性的元数据
    StructDescriptorRegistry<ExampleStruct>::descriptor
        .add("intValue", PropertyMetadata("个", 0, 100))  // 单位为"个"，范围0-100
        .add("floatValue", PropertyMetadata("kg", 0.0f, 100.0f, 3))  // 单位为"kg"，精度为3
        .add("doubleValue", PropertyMetadata("m", 0.0, 1000.0, 4))  // 单位为"m"，精度为4
        .add("intArray", PropertyMetadata("intlist"))  // 整数列表类型
        .add("floatArray", PropertyMetadata("floatlist"))  // 浮点数列表类型
        .add("doubleArray", PropertyMetadata("doublelist"))  // 双精度列表类型
        .add("intMap", PropertyMetadata("intmap"))  // 整数映射类型
        .add("floatMap", PropertyMetadata("floatmap"))  // 浮点数映射类型
        .add("doubleMap", PropertyMetadata("doublemap"));  // 双精度映射类型
}

/**
 * @brief 特化ExampleStruct的toStruct模板
 */
template<>
inline bool QPropertyObject::toStruct<ExampleStruct>(ExampleStruct &structObj) const
{
    if (m_type != PropertyType::Object) {
        return false;
    }
    
    // 读取基本类型属性
    QPropertyObject *intValueProp = findChild("intValue");
    if (intValueProp && intValueProp->value().canConvert<int>()) {
        structObj.intValue = intValueProp->value().toInt();
    }
    
    QPropertyObject *floatValueProp = findChild("floatValue");
    if (floatValueProp && floatValueProp->value().canConvert<float>()) {
        structObj.floatValue = floatValueProp->value().toFloat();
    }
    
    QPropertyObject *doubleValueProp = findChild("doubleValue");
    if (doubleValueProp && doubleValueProp->value().canConvert<double>()) {
        structObj.doubleValue = doubleValueProp->value().toDouble();
    }
    
    // 读取数组属性
    QPropertyObject *intArrayProp = findChild("intArray");
    if (intArrayProp && intArrayProp->isArray()) {
        structObj.intArray.clear();
        for (auto item : intArrayProp->arrayItems()) {
            if (item && item->value().canConvert<int>()) {
                structObj.intArray.push_back(item->value().toInt());
            }
        }
    }
    
    QPropertyObject *floatArrayProp = findChild("floatArray");
    if (floatArrayProp && floatArrayProp->isArray()) {
        structObj.floatArray.clear();
        for (auto item : floatArrayProp->arrayItems()) {
            if (item && item->value().canConvert<float>()) {
                structObj.floatArray.push_back(item->value().toFloat());
            }
        }
    }
    
    QPropertyObject *doubleArrayProp = findChild("doubleArray");
    if (doubleArrayProp && doubleArrayProp->isArray()) {
        structObj.doubleArray.clear();
        for (auto item : doubleArrayProp->arrayItems()) {
            if (item && item->value().canConvert<double>()) {
                structObj.doubleArray.push_back(item->value().toDouble());
            }
        }
    }
    
    // 读取映射属性
    QPropertyObject *intMapProp = findChild("intMap");
    if (intMapProp && intMapProp->isMap()) {
        structObj.intMap.clear();
        auto mapItems = intMapProp->mapItems();
        for (auto it = mapItems.constBegin(); it != mapItems.constEnd(); ++it) {
            if (it.value() && it.value()->value().canConvert<int>()) {
                structObj.intMap[it.key().toStdString()] = it.value()->value().toInt();
            }
        }
    }
    
    QPropertyObject *floatMapProp = findChild("floatMap");
    if (floatMapProp && floatMapProp->isMap()) {
        structObj.floatMap.clear();
        auto mapItems = floatMapProp->mapItems();
        for (auto it = mapItems.constBegin(); it != mapItems.constEnd(); ++it) {
            if (it.value() && it.value()->value().canConvert<float>()) {
                structObj.floatMap[it.key().toStdString()] = it.value()->value().toFloat();
            }
        }
    }
    
    QPropertyObject *doubleMapProp = findChild("doubleMap");
    if (doubleMapProp && doubleMapProp->isMap()) {
        structObj.doubleMap.clear();
        auto mapItems = doubleMapProp->mapItems();
        for (auto it = mapItems.constBegin(); it != mapItems.constEnd(); ++it) {
            if (it.value() && it.value()->value().canConvert<double>()) {
                structObj.doubleMap[it.key().toStdString()] = it.value()->value().toDouble();
            }
        }
    }
    
    return true;
}