#pragma once
#include <QDialog>
#include <QString>
class QPushButton;
namespace vsgraysim::gui::display {
class WorkbenchSelector final:public QDialog {
public:
    explicit WorkbenchSelector(QWidget* parent=nullptr);
    QString selectedMode()const{return mode_;}
    QString equipmentConfig()const{return equipment_;}
private:
    QString mode_,equipment_;
};
class ConsoleSelector final:public QDialog {
public:
    explicit ConsoleSelector(QWidget* parent=nullptr,const QString& preferred={});
    QString selectedProfile()const{return selected_;}
private:
    QString selected_;
};
}
