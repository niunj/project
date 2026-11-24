#pragma once
#include <QObject>
#include <QStringList>
#include <nlohmann/json.hpp>
#include <array>
#include <bitset>
#include <functional>
#include <optional>
class QSocketNotifier;
namespace vsgraysim::gui::display {
// Optional Linux joystick reader. Receiving input never acquires a control lease;
// the owning panel must explicitly arm both its lease and hardware input.
class JoystickInput final:public QObject {
public:
    explicit JoystickInput(QObject* parent=nullptr):QObject(parent){}
    ~JoystickInput()override;
    static QStringList devices();
    static void validateMapping(const nlohmann::json&);
    void configure(const nlohmann::json&);
    bool openDevice(const QString&,QString& error);
    void closeDevice();
    bool connected()const{return fd_>=0;}
    QString name()const{return name_;}
    // Public decoder boundary also supports deterministic recorded-device tests.
    void consumePacket(unsigned char type,unsigned char number,short value);
    std::optional<std::array<double,4>> demands()const;
    std::function<void(const std::array<double,4>&)> axesChanged;
    std::function<void()> releasePressed;
    std::function<void(QString)> disconnected;
private:
    void readReady();
    std::array<int,4> axes_{{2,1,0,3}}; // throttle, pitch, roll, rudder
    std::array<bool,4> invert_{{true,false,false,false}};
    std::array<short,32> raw_{};
    std::bitset<32> seen_;
    double deadzone_=.04;
    int releaseButton_=7,fd_=-1;
    QString name_;
    QSocketNotifier* notifier_=nullptr;
};
}
