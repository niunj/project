#include "FileAssociation.h"
#include <QApplication>
#include <QMessageBox>
#include <QCommandLineParser>
#include <QSettings>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QDir>

// 控制注册提示频率
bool shouldShowRegPrompt()
{
    QSettings settings("PMTXEditor", "UserConfig");
    qint64 lastPromptTime = settings.value("LastRegPromptSec", 0).toLongLong();
    qint64 currentTime = QDateTime::currentSecsSinceEpoch();
    const qint64 PROMPT_INTERVAL = 604800; // 7天
    
    if (currentTime - lastPromptTime > PROMPT_INTERVAL) {
        settings.setValue("LastRegPromptSec", currentTime);
        return true;
    }
    return false;
}


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName("PMTX数据编辑器");
    a.setApplicationVersion("1.2.0");
    a.setOrganizationName("PMTX Tools");

    // 1. 处理文件关联（删除管理员权限检查）
    QString appExePath = QApplication::applicationFilePath();

    QString appIconPath = QApplication::applicationDirPath()  + "icons/ptx.ico"; // 图标文件路径

    qDebug() << appExePath << appIconPath;
    bool hasRegistered = FileAssociation::isPmtxRegistered();

    if (!hasRegistered)
    {
        bool regSuccess = FileAssociation::registerPmtxType(appExePath, appIconPath);
        if (shouldShowRegPrompt())
        {
            if (regSuccess)
            {
                QMessageBox::information(nullptr, "文件关联成功", ".pmtx文件已关联到本程序，双击即可打开。");
            }
            else
            {
                // 删掉管理员提示，改为通用提示
                QMessageBox::warning(nullptr, "注册失败", "无法自动关联.pmtx文件，可手动右键文件→打开方式→选择本程序。");
            }
        }
    }

    // 解析命令行参数
    QCommandLineParser parser;
    parser.addPositionalArgument("file", "要打开的.pmtx文件", "[file]");
    parser.process(a);
    
    QString openFilePath;
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        openFilePath = QFileInfo(args.first()).absoluteFilePath();
    }

    // 启动主窗口
    QWidget w;
    QHBoxLayout* l = new QHBoxLayout;
    l->addWidget(new QLabel(openFilePath));


    w.setLayout(l);
    if (!openFilePath.isEmpty()) {
        qDebug()<< "调用程序打开工工程逻辑";
//        w.openPmtxFile(openFilePath);
    }
    w.show();

    return a.exec();
}
