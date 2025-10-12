#include "mlsreader.h"
#include <QFile>
#include <QDebug>

MlsReader::MlsReader()
{
}

bool MlsReader::read(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "无法打开MLS文件:" << filePath << "错误:" << file.errorString();
        return false;
    }
    
    // 读取文件内容
    m_data = file.readAll();
    file.close();
    
    // 这里可以添加MLS文件格式解析逻辑
    // MLS不是标准格式，具体解析方式取决于实际文件格式
    
    qDebug() << "成功读取MLS文件:" << filePath << "大小:" << m_data.size() << "字节";
    return true;
}
