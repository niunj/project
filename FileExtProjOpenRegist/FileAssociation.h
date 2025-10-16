#ifndef FILEASSOCIATION_H
#define FILEASSOCIATION_H

#include <QString>

class FileAssociation
{
public:
    // 注册.pmtx文件类型（根据系统自动适配）
    static bool registerPmtxType(const QString& appPath, const QString& iconPath = "");

    // 检查.pmtx是否已注册
    static bool isPmtxRegistered();

    // 取消注册（可选）
    static bool unregisterPmtxType();

private:
    // Linux平台实现
    static bool registerPmtxTypeLinux(const QString& appPath, const QString& iconPath);
    static bool isPmtxRegisteredLinux();
    static bool unregisterPmtxTypeLinux();

    // Windows平台实现
    static bool registerPmtxTypeWindows(const QString& appPath, const QString& iconPath);
    static bool isPmtxRegisteredWindows();
    static bool unregisterPmtxTypeWindows();
};

#endif // FILEASSOCIATION_H
