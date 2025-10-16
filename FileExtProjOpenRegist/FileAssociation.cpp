#include "FileAssociation.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QProcess>
#include <QSettings>
#include <QDebug>
#include <QFileInfo>


#ifdef Q_OS_WIN
// 补充必要的 Windows 头文件，确保宏定义被加载
#include <windows.h>
#include <shlobj.h>   // 包含 SHCNE_ASSOCCHANGED/SHCNF_IDLIST 宏定义
#include <shellapi.h> // 包含 SHChangeNotify 函数声明
#pragma comment(lib, "shell32.lib")  // 链接 shell32 库（MSVC 专用）
#pragma comment(lib, "ole32.lib")    // 补充依赖库，避免潜在链接错误
#endif

// 跨平台入口函数
bool FileAssociation::registerPmtxType(const QString& appPath, const QString& iconPath)
{
//#ifdef Q_OS_LINUX
//    return registerPmtxTypeLinux(appPath, iconPath);
//#elif defined(Q_OS_WIN)
//    return registerPmtxTypeWindows(appPath, iconPath);
//#else
//    qWarning() << "不支持的操作系统";
//    return false;
//#endif

    //目前只保留window处理
#ifdef Q_OS_WIN
    return registerPmtxTypeWindows(appPath, iconPath);
#else
    return false;
#endif

}

bool FileAssociation::isPmtxRegistered()
{
//#ifdef Q_OS_LINUX
//    return isPmtxRegisteredLinux();
//#elif defined(Q_OS_WIN)
//    return isPmtxRegisteredWindows();
//#else
//    return false;
//#endif

#ifdef Q_OS_WIN
    return isPmtxRegisteredWindows();
#else
    return false;
#endif
}

bool FileAssociation::unregisterPmtxType()
{
#ifdef Q_OS_LINUX
    return unregisterPmtxTypeLinux();
#elif defined(Q_OS_WIN)
    return unregisterPmtxTypeWindows();
#else
    return false;
#endif
}

// --------------------------
// Linux平台实现
// --------------------------
namespace LinuxFileAssoc {
    const QString APP_NAME = "pmtx_editor";
    const QString DESKTOP_FILENAME = APP_NAME + ".desktop";
    const QString MIME_TYPE = "application/x-pmtx";
    const QString MIME_FILENAME = "x-pmtx.xml";

    // 获取用户目录下的配置路径（无需管理员权限）
    QString getApplicationsDir() {
        return QDir::homePath() + "/.local/share/applications/";
    }

    QString getMimePackagesDir() {
        return QDir::homePath() + "/.local/share/mime/packages/";
    }
}

bool FileAssociation::registerPmtxTypeLinux(const QString& appPath, const QString& iconPath)
{
    using namespace LinuxFileAssoc;

    // 1. 创建.desktop文件（描述应用信息）
    QDir appsDir(getApplicationsDir());
    if (!appsDir.exists() && !appsDir.mkpath(".")) {
        qWarning() << "创建applications目录失败:" << appsDir.path();
        return false;
    }

    QString desktopPath = appsDir.filePath(DESKTOP_FILENAME);
    QFile desktopFile(desktopPath);
    if (!desktopFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "打开desktop文件失败:" << desktopFile.errorString();
        return false;
    }

    QTextStream out(&desktopFile);
    out << "[Desktop Entry]\n";
    out << "Name=PMTX编辑器\n";
    out << "Comment=打开和编辑PMTX格式文件\n";
    out << "Exec=\"" << appPath << "\" \"%f\"\n"; // %f表示双击的文件路径
    out << "Icon=" << (iconPath.isEmpty() ? "package-x-generic" : iconPath) << "\n";
    out << "Type=Application\n";
    out << "MimeType=" << MIME_TYPE << ";\n";
    out << "Categories=Utility;Development;\n";
    desktopFile.close();

    // 2. 创建MIME类型定义文件（关联.pmtx后缀）
    QDir mimeDir(getMimePackagesDir());
    if (!mimeDir.exists() && !mimeDir.mkpath(".")) {
        qWarning() << "创建mime目录失败:" << mimeDir.path();
        return false;
    }

    QString mimePath = mimeDir.filePath(MIME_FILENAME);
    QFile mimeFile(mimePath);
    if (!mimeFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "打开mime文件失败:" << mimeFile.errorString();
        return false;
    }

    out.setDevice(&mimeFile);
    out << "<?xml version=\"1.0\"?>\n";
    out << "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n";
    out << "  <mime-type type=\"" << MIME_TYPE << "\">\n";
    out << "    <comment>PMTX数据文件</comment>\n";
    out << "    <glob pattern=\"*.pmtx\"/>\n"; // 关联.pmtx后缀
    out << "  </mime-type>\n";
    out << "</mime-info>\n";
    mimeFile.close();

    // 3. 更新系统缓存使配置生效
    QProcess::execute("update-mime-database", {getMimePackagesDir()});
    QProcess::execute("update-desktop-database", {getApplicationsDir()});

    return true;
}

bool FileAssociation::isPmtxRegisteredLinux()
{
    using namespace LinuxFileAssoc;

    // 检查desktop文件是否存在
    if (!QFile::exists(getApplicationsDir() + DESKTOP_FILENAME))
        return false;

    // 检查mime文件是否存在
    if (!QFile::exists(getMimePackagesDir() + MIME_FILENAME))
        return false;

    // 检查关联是否生效
    QProcess process;
    process.start("xdg-mime", {"query", "default", MIME_TYPE});
    process.waitForFinished();
    QString result = process.readAllStandardOutput().trimmed();
    return result == DESKTOP_FILENAME;
}

bool FileAssociation::unregisterPmtxTypeLinux()
{
    using namespace LinuxFileAssoc;

    // 删除配置文件
    QFile::remove(getApplicationsDir() + DESKTOP_FILENAME);
    QFile::remove(getMimePackagesDir() + MIME_FILENAME);

    // 更新缓存
    QProcess::execute("update-mime-database", {getMimePackagesDir()});
    QProcess::execute("update-desktop-database", {getApplicationsDir()});
    return true;
}

// --------------------------
// Windows平台实现
// --------------------------
namespace WindowsFileAssoc {
    const QString EXTENSION = ".pmtx";
    const QString PROGID = "pmtxfile";
    const QString DESCRIPTION = "PMTX数据文件";
}

bool FileAssociation::registerPmtxTypeWindows(const QString& appPath, const QString& iconPath)
{
    using namespace WindowsFileAssoc;

    // 路径处理（包含空格时需用双引号）
    QString quotedApp = QString("\"%1\"").arg(appPath);
    QString icon = iconPath.isEmpty() ? quotedApp : QString("\"%1\"").arg(iconPath);

    // 1. 注册扩展名关联
    QSettings extReg(EXTENSION, QSettings::NativeFormat);
    extReg.setValue("", PROGID);
    extReg.setValue("Content Type", "application/x-pmtx");

    // 2. 注册程序ID信息
    QSettings progidReg(PROGID, QSettings::NativeFormat);
    progidReg.setValue("", DESCRIPTION);
    progidReg.setValue("DefaultIcon", icon);

    // 3. 注册打开命令
    QSettings cmdReg(QString("%1\\shell\\open\\command").arg(PROGID), QSettings::NativeFormat);
    cmdReg.setValue("", QString("%1 \"%1\"").arg(quotedApp).arg("%1")); // %1会替换为文件路径

    // 刷新系统缓存
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool FileAssociation::isPmtxRegisteredWindows()
{
    using namespace WindowsFileAssoc;

    // 检查扩展名关联
    QSettings extReg(EXTENSION, QSettings::NativeFormat);
    if (extReg.value("").toString() != PROGID)
        return false;

    // 检查打开命令是否正确
    QSettings cmdReg(QString("%1\\shell\\open\\command").arg(PROGID), QSettings::NativeFormat);
    QString cmd = cmdReg.value("").toString();
    return cmd.contains(QFileInfo(QCoreApplication::applicationFilePath()).fileName());
}

bool FileAssociation::unregisterPmtxTypeWindows()
{
    using namespace WindowsFileAssoc;

    QSettings extReg(EXTENSION, QSettings::NativeFormat);
    extReg.remove("");

    QSettings progidReg(PROGID, QSettings::NativeFormat);
    progidReg.remove("");

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}
