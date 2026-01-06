#include "loginfo.h"
#include "ui_loginfo.h"
#include <QDateTime>
#include <QScrollBar>

LogInfo::LogInfo(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::LogInfo)
{
    ui->setupUi(this);

}

LogInfo::~LogInfo()
{
    delete ui;
}

void LogInfo::addLog(QString logInfo)
{
    // 获取当前时间
    QString time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    ui->textEdit->append(QString("%1-%2").arg(time).arg(logInfo));

    // 2. 滚动到最下方（可选，根据需求决定是否自动滚动）
    QScrollBar* verticalScrollBar = ui->textEdit->verticalScrollBar();
    if (verticalScrollBar) {
        verticalScrollBar->setValue(verticalScrollBar->maximum()); // 滚动到最底部
    }
}

