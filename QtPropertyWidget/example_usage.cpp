#include "qpropertyobject.h"
#include "qpropertyobject_struct.h"
#include <iostream>

// 定义一个嵌套的结构体
struct NestedStruct {
    int nestedInt;
    float nestedFloat;
    std::string nestedString;
};

// 在ExampleStruct中添加嵌套结构体字段
template<>
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
    
    // 添加嵌套结构体字段
    NestedStruct nestedStruct;
    std::vector<NestedStruct> nestedArray;
    std::map<std::string, NestedStruct> nestedMap;
};

// 特化NestedStruct的fromStruct模板
template<>
inline QPropertyObject *QPropertyObject::fromStruct<NestedStruct>(const NestedStruct &structObj, const QString &name)
{
    QPropertyObject *obj = new QPropertyObject(name);
    obj->setType(PropertyType::Object);
    
    // 添加嵌套结构体的基本属性
    obj->addChild("nestedInt", structObj.nestedInt);
    obj->addChild("nestedFloat", structObj.nestedFloat);
    obj->addChild("nestedString", QString::fromStdString(structObj.nestedString));
    
    return obj;
}

// 特化NestedStruct的toStruct模板
template<>
inline NestedStruct QPropertyObject::toStruct<NestedStruct>(const QPropertyObject *obj)
{
    NestedStruct structObj;
    
    if (obj) {
        structObj.nestedInt = obj->findChild("nestedInt")->value().toInt();
        structObj.nestedFloat = obj->findChild("nestedFloat")->value().toFloat();
        structObj.nestedString = obj->findChild("nestedString")->value().toString().toStdString();
    }
    
    return structObj;
}

// 更新ExampleStruct的fromStruct特化，支持嵌套结构体
template<>
inline QPropertyObject *QPropertyObject::fromStruct<ExampleStruct>(const ExampleStruct &structObj, const QString &name)
{
    // 首先调用基本实现
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
    
    // 添加嵌套结构体
    obj->addChild("nestedStruct", structObj.nestedStruct);
    
    // 添加嵌套结构体数组
    QPropertyObject *nestedArrayProp = obj->addChild("nestedArray");
    nestedArrayProp->setType(PropertyType::Array);
    nestedArrayProp->setupArray();
    for (const auto &nested : structObj.nestedArray) {
        nestedArrayProp->addChild(fromStruct<NestedStruct>(nested));
    }
    
    // 添加嵌套结构体映射
    QPropertyObject *nestedMapProp = obj->addChild("nestedMap");
    nestedMapProp->setType(PropertyType::Map);
    nestedMapProp->setupMap();
    for (const auto &pair : structObj.nestedMap) {
        nestedMapProp->setMapItem(QString::fromStdString(pair.first), fromStruct<NestedStruct>(pair.second));
    }
    
    return obj;
}

// 更新ExampleStruct的toStruct特化，支持嵌套结构体
template<>
inline ExampleStruct QPropertyObject::toStruct<ExampleStruct>(const QPropertyObject *obj)
{
    ExampleStruct structObj;
    
    if (obj) {
        structObj.intValue = obj->findChild("intValue")->value().toInt();
        structObj.floatValue = obj->findChild("floatValue")->value().toFloat();
        structObj.doubleValue = obj->findChild("doubleValue")->value().toDouble();
        
        // 处理数组
        QPropertyObject *intArrayObj = obj->findChild("intArray");
        if (intArrayObj && intArrayObj->isArray()) {
            for (int i = 0; i < intArrayObj->childCount(); i++) {
                structObj.intArray.push_back(intArrayObj->child(i)->value().toInt());
            }
        }
        
        QPropertyObject *floatArrayObj = obj->findChild("floatArray");
        if (floatArrayObj && floatArrayObj->isArray()) {
            for (int i = 0; i < floatArrayObj->childCount(); i++) {
                structObj.floatArray.push_back(floatArrayObj->child(i)->value().toFloat());
            }
        }
        
        QPropertyObject *doubleArrayObj = obj->findChild("doubleArray");
        if (doubleArrayObj && doubleArrayObj->isArray()) {
            for (int i = 0; i < doubleArrayObj->childCount(); i++) {
                structObj.doubleArray.push_back(doubleArrayObj->child(i)->value().toDouble());
            }
        }
        
        // 处理映射
        QPropertyObject *intMapObj = obj->findChild("intMap");
        if (intMapObj && intMapObj->isMap()) {
            for (int i = 0; i < intMapObj->childCount(); i++) {
                QString key = intMapObj->child(i)->name();
                if (key.startsWith("@map:")) {
                    key = key.mid(5);
                    structObj.intMap[key.toStdString()] = intMapObj->child(i)->value().toInt();
                }
            }
        }
        
        QPropertyObject *floatMapObj = obj->findChild("floatMap");
        if (floatMapObj && floatMapObj->isMap()) {
            for (int i = 0; i < floatMapObj->childCount(); i++) {
                QString key = floatMapObj->child(i)->name();
                if (key.startsWith("@map:")) {
                    key = key.mid(5);
                    structObj.floatMap[key.toStdString()] = floatMapObj->child(i)->value().toFloat();
                }
            }
        }
        
        QPropertyObject *doubleMapObj = obj->findChild("doubleMap");
        if (doubleMapObj && doubleMapObj->isMap()) {
            for (int i = 0; i < doubleMapObj->childCount(); i++) {
                QString key = doubleMapObj->child(i)->name();
                if (key.startsWith("@map:")) {
                    key = key.mid(5);
                    structObj.doubleMap[key.toStdString()] = doubleMapObj->child(i)->value().toDouble();
                }
            }
        }
        
        // 处理嵌套结构体
        QPropertyObject *nestedStructObj = obj->findChild("nestedStruct");
        if (nestedStructObj) {
            structObj.nestedStruct = toStruct<NestedStruct>(nestedStructObj);
        }
        
        // 处理嵌套结构体数组
        QPropertyObject *nestedArrayObj = obj->findChild("nestedArray");
        if (nestedArrayObj && nestedArrayObj->isArray()) {
            for (int i = 0; i < nestedArrayObj->childCount(); i++) {
                structObj.nestedArray.push_back(toStruct<NestedStruct>(nestedArrayObj->child(i)));
            }
        }
        
        // 处理嵌套结构体映射
        QPropertyObject *nestedMapObj = obj->findChild("nestedMap");
        if (nestedMapObj && nestedMapObj->isMap()) {
            for (int i = 0; i < nestedMapObj->childCount(); i++) {
                QString key = nestedMapObj->child(i)->name();
                if (key.startsWith("@map:")) {
                    key = key.mid(5);
                    structObj.nestedMap[key.toStdString()] = toStruct<NestedStruct>(nestedMapObj->child(i));
                }
            }
        }
    }
    
    return structObj;
}

// 主函数，演示使用
int main() {
    // 注册结构体描述器
    registerExampleStructDescriptor();
    
    // 创建一个示例结构体
    ExampleStruct example;
    example.intValue = 42;
    example.floatValue = 3.14f;
    example.doubleValue = 2.71828;
    
    // 添加数组数据
    example.intArray = {1, 2, 3, 4, 5};
    example.floatArray = {1.1f, 2.2f, 3.3f};
    example.doubleArray = {1.11, 2.22, 3.33};
    
    // 添加映射数据
    example.intMap["one"] = 1;
    example.intMap["two"] = 2;
    example.floatMap["pi"] = 3.14f;
    example.doubleMap["e"] = 2.71828;
    
    // 设置嵌套结构体
    example.nestedStruct.nestedInt = 100;
    example.nestedStruct.nestedFloat = 99.9f;
    example.nestedStruct.nestedString = "Hello Nested";
    
    // 添加嵌套结构体数组
    NestedStruct nested1, nested2;
    nested1.nestedInt = 101;
    nested1.nestedFloat = 101.1f;
    nested1.nestedString = "Nested 1";
    
    nested2.nestedInt = 102;
    nested2.nestedFloat = 102.2f;
    nested2.nestedString = "Nested 2";
    
    example.nestedArray = {nested1, nested2};
    
    // 添加嵌套结构体映射
    example.nestedMap["nestedA"] = nested1;
    example.nestedMap["nestedB"] = nested2;
    
    // 将结构体转换为属性对象
    QPropertyObject *propObj = QPropertyObject::fromStruct(example, "ExampleObject");
    
    // 输出属性对象的信息（演示如何访问元数据）
    std::cout << "Property Object: " << propObj->name().toStdString() << std::endl;
    
    QPropertyObject *intProp = propObj->findChild("intValue");
    if (intProp) {
        std::cout << "intValue: " << intProp->value().toInt() << " " 
                  << intProp->unit().toStdString() << " (range: " 
                  << intProp->minValue().toInt() << "-" 
                  << intProp->maxValue().toInt() << ")" << std::endl;
    }
    
    QPropertyObject *floatProp = propObj->findChild("floatValue");
    if (floatProp) {
        std::cout << "floatValue: " << floatProp->value().toFloat() << " " 
                  << floatProp->unit().toStdString() << " (precision: " 
                  << floatProp->precision() << ")" << std::endl;
    }
    
    // 演示嵌套结构体访问
    QPropertyObject *nestedObj = propObj->findChild("nestedStruct");
    if (nestedObj) {
        std::cout << "Nested Struct:" << std::endl;
        std::cout << "  nestedInt: " << nestedObj->findChild("nestedInt")->value().toInt() << std::endl;
        std::cout << "  nestedString: " << nestedObj->findChild("nestedString")->value().toString().toStdString() << std::endl;
    }
    
    // 清理内存
    delete propObj;
    
    return 0;
}