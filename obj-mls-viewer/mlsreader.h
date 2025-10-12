#ifndef MLSREADER_H
#define MLSREADER_H

#include <QString>

// MLS文件读取器
class MlsReader
{
public:
    MlsReader();
    
    // 读取MLS文件
    bool read(const QString &filePath);
    
    // 获取读取的数据（根据实际需求扩展）
    // 这里只是示例，实际应根据MLS文件格式定义数据结构
    const QByteArray& getData() const { return m_data; }
    
private:
    QByteArray m_data; // 存储读取的MLS数据
};

#endif // MLSREADER_H
