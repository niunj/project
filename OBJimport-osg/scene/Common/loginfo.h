#ifndef LOGINFO_H
#define LOGINFO_H

#include <QWidget>

namespace Ui {
class LogInfo;
}

class LogInfo : public QWidget
{
    Q_OBJECT

public:
    explicit LogInfo(QWidget *parent = nullptr);
    ~LogInfo();

public slots:
    void addLog(const QString logInfo);

private:
    Ui::LogInfo *ui;
};

#endif // LOGINFO_H
