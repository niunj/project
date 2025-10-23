#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qpropertyobject_struct.h"
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    
    // 测试结构体与QPropertyObject的转换
    testStructConversion();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::testStructConversion()
{
    qDebug() << "Testing struct conversion with QPropertyObject...";
    
    // 创建测试结构体
    ExampleStruct testStruct;
    testStruct.intValue = 42;
    testStruct.floatValue = 3.14f;
    testStruct.doubleValue = 2.718281828;
    
    // 填充数组
    testStruct.intArray = {1, 2, 3, 4, 5};
    testStruct.floatArray = {1.1f, 2.2f, 3.3f};
    testStruct.doubleArray = {1.11, 2.22, 3.33, 4.44};
    
    // 填充映射
    testStruct.intMap["one"] = 1;
    testStruct.intMap["two"] = 2;
    testStruct.floatMap["pi"] = 3.14f;
    testStruct.doubleMap["e"] = 2.71828;
    
    qDebug() << "Original struct created";
    
    // 转换为QPropertyObject
    QPropertyObject *propObj = QPropertyObject::fromStruct(testStruct, "TestStruct");
    qDebug() << "Converted struct to QPropertyObject";
    
    // 测试XML序列化
    QString xmlFileName = QCoreApplication::applicationDirPath() + "/test_struct.xml";
    if (propObj->saveToXmlFile(xmlFileName)) {
        qDebug() << "Saved to XML file:" << xmlFileName;
    }
    
    // 测试二进制序列化
    QString binFileName = QCoreApplication::applicationDirPath() + "/test_struct.bin";
    if (propObj->saveToBinaryFile(binFileName)) {
        qDebug() << "Saved to binary file:" << binFileName;
    }
    
    // 创建新的结构体用于测试反转换
    ExampleStruct convertedStruct;
    
    // 从QPropertyObject转换回结构体
    if (propObj->toStruct(convertedStruct)) {
        qDebug() << "Converted QPropertyObject back to struct";
        
        // 验证转换结果
        qDebug() << "Verifying conversion results:";
        qDebug() << "intValue:" << convertedStruct.intValue;
        qDebug() << "floatValue:" << convertedStruct.floatValue;
        qDebug() << "doubleValue:" << convertedStruct.doubleValue;
        qDebug() << "intArray size:" << convertedStruct.intArray.size();
        qDebug() << "floatArray size:" << convertedStruct.floatArray.size();
        qDebug() << "doubleArray size:" << convertedStruct.doubleArray.size();
        qDebug() << "intMap size:" << convertedStruct.intMap.size();
        qDebug() << "floatMap size:" << convertedStruct.floatMap.size();
        qDebug() << "doubleMap size:" << convertedStruct.doubleMap.size();
    }
    
    // 清理
    delete propObj;
    
    qDebug() << "Struct conversion test completed";
}
