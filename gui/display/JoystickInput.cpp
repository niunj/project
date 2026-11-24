#include "JoystickInput.h"
#include <QDir>
#include <QSocketNotifier>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#ifdef __linux__
#include <linux/joystick.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cerrno>
#endif

namespace vsgraysim::gui::display {
JoystickInput::~JoystickInput(){closeDevice();}
QStringList JoystickInput::devices(){QStringList result;
#ifdef __linux__
    const QDir dir("/dev/input");for(const auto& file:dir.entryList({"js*"},QDir::System|QDir::Files,QDir::Name))result.push_back(dir.filePath(file));
#endif
    return result;
}
void JoystickInput::validateMapping(const nlohmann::json& j){
    if(!j.is_object())throw std::invalid_argument("joystick mapping must be an object");
    std::bitset<32> seen;int defaults[4]={2,1,0,3};int index=0;
    for(const auto* key:{"throttle_axis","pitch_axis","roll_axis","rudder_axis"}){const auto v=j.value(key,nlohmann::json(defaults[index++]));if(!v.is_number_integer()||v.get<double>()<0||v.get<double>()>31||seen.test(v.get<unsigned>()))throw std::invalid_argument("invalid or duplicate joystick axis");seen.set(v.get<unsigned>());}
    for(const auto* key:{"invert_throttle","invert_pitch","invert_roll","invert_rudder"})if(j.contains(key)&&!j[key].is_boolean())throw std::invalid_argument("invalid joystick axis direction");
    const double dead=j.value("deadzone",.04);if(!std::isfinite(dead)||dead<0||dead>.4)throw std::invalid_argument("invalid joystick deadzone");
    const auto release=j.value("release_button",nlohmann::json(7));if(!release.is_number_integer()||release.get<double>()<0||release.get<double>()>31)throw std::invalid_argument("invalid joystick release button");
}
void JoystickInput::configure(const nlohmann::json& j){validateMapping(j);axes_={j.value("throttle_axis",2),j.value("pitch_axis",1),j.value("roll_axis",0),j.value("rudder_axis",3)};invert_={j.value("invert_throttle",true),j.value("invert_pitch",false),j.value("invert_roll",false),j.value("invert_rudder",false)};deadzone_=j.value("deadzone",.04);releaseButton_=j.value("release_button",7);raw_.fill(0);seen_.reset();}
bool JoystickInput::openDevice(const QString& path,QString& error){closeDevice();
#ifdef __linux__
    if(!devices().contains(path)){error=tr("请选择已发现的手柄设备");return false;}
    fd_=::open(path.toLocal8Bit().constData(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd_<0){error=tr("无法读取手柄，请检查设备访问权限");return false;}
    char deviceName[129]={};if(::ioctl(fd_,JSIOCGNAME(sizeof(deviceName)-1),deviceName)>=0)name_=QString::fromLocal8Bit(deviceName);else name_=tr("USB Joystick");
    raw_.fill(0);seen_.reset();notifier_=new QSocketNotifier(fd_,QSocketNotifier::Read,this);connect(notifier_,&QSocketNotifier::activated,this,[this]{readReady();});error.clear();return true;
#else
    Q_UNUSED(path);error=tr("当前构建未提供本机手柄驱动适配");return false;
#endif
}
void JoystickInput::closeDevice(){if(notifier_){notifier_->setEnabled(false);notifier_->deleteLater();notifier_=nullptr;}
#ifdef __linux__
    if(fd_>=0)::close(fd_);
#endif
    fd_=-1;seen_.reset();name_.clear();
}
std::optional<std::array<double,4>> JoystickInput::demands()const{std::array<double,4> result{};for(int i=0;i<4;++i){if(!seen_.test(axes_[i]))return {};double value=std::clamp(raw_[axes_[i]]/32767.,-1.,1.);if(invert_[i])value=-value;
        if(i==0)result[i]=(value+1)*.5;else result[i]=std::abs(value)<=deadzone_?0:std::copysign((std::abs(value)-deadzone_)/(1-deadzone_),value);}return result;}
void JoystickInput::consumePacket(unsigned char type,unsigned char number,short value){const auto event=type&~0x80;if(event==2&&number<raw_.size()){raw_[number]=value;seen_.set(number);if(auto axes=demands();axes&&axesChanged)axesChanged(*axes);}else if(event==1&&!(type&0x80)&&number==releaseButton_&&value&&releasePressed)releasePressed();}
void JoystickInput::readReady(){
#ifdef __linux__
    for(int received=0;received<256&&fd_>=0;++received){js_event packet{};const auto count=::read(fd_,&packet,sizeof(packet));if(count==sizeof(packet)){consumePacket(packet.type,packet.number,packet.value);continue;}if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;if(count<0&&errno==EINTR)continue;closeDevice();if(disconnected)disconnected(tr("手柄已断开或读取失败"));break;}
#endif
}
}
