#include "platform_animation/F16CAnimation.h"
#include "platform_animation/PlatformAnimation.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace vsgraysim::platform_animation;

namespace {
void require(bool condition, const char* message) {
    if(!condition)throw std::runtime_error(message);
}

double angle(const AnimationFrame& frame, std::string_view joint) {
    const auto found=std::find_if(frame.joints.begin(),frame.joints.end(),[&](const JointPose& pose){ return pose.joint==joint; });
    require(found!=frame.joints.end(),"missing sampled joint");
    return found->angle;
}
}

int main() try {
    auto tank=makePlatformAnimation("m1a2");
    auto jet=makePlatformAnimation("f16c");
    auto secondJet=makePlatformAnimation("f16c");
    auto ship=makePlatformAnimation("cg57");
    require(tank&&jet&&secondJet&&ship&&!makePlatformAnimation("unknown"),"model selection");
    AnimationFrame frame, other;
    tank->sample({.9,.8,1.2},frame);
    const auto yaw=angle(frame,"turret");
    require(yaw>.48&&angle(frame,"elevation")>.08&&frame.effects.empty(),"pre-cue turret and barrel");
    tank->sample({2.6,2.5,1.2},frame);
    require(angle(frame,"turret")<-.3,"opposite turret yaw");
    tank->sample({.9,.8,1.2},frame);
    require(angle(frame,"turret")==yaw,"seek must reproduce the sampled pose");

    jet->sample({1.,.9,std::nullopt,.95},frame);
    secondJet->sample({1.,.9,std::nullopt,.2},other);
    require(frame.effects.size()==1&&frame.effects[0].style==EffectStyle::Jet&&
            frame.effects[0].strength>.8f&&frame.effects[0].exhaust.smokeOpacity==.12f&&other.effects.empty(),"independent jet throttle effects");
    auto nozzle=jet->modelAttachment(frame.effects[0].attachment,15.);
    require(nozzle&&*nozzle==f16NozzleAt15m,"nozzle binding");
    ship->sample({1.,.9,1.2},frame);
    require(angle(frame,"left_cover")>1.3&&angle(frame,"right_cover")<-1.3,"independent ship curves");

    const auto* cockpit=findCockpitModel("f16c");
    require(cockpit&&!findCockpitModel("m1a2"),"cockpit selection");
    const auto gear=cockpit->command(CabinAction::LandingGear);
    require(gear.signal==CabinSignal::AircraftSystem&&gear.system==AircraftSystem::LandingGear,"gear signal mapping");
    require(cockpit->command(CabinAction::PanelLight).signal==CabinSignal::PanelLight,"local lamp signal");
    CabinState confirmed;
    const auto before=cockpit->pose(confirmed);
    confirmed.gear=true;
    confirmed.aileron=.5;
    const auto after=cockpit->pose(confirmed);
    require(before.gear<0&&after.gear>0&&after.stickRoll>.07,"pose follows confirmed telemetry");
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
