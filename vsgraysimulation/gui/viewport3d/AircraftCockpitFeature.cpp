#include "AircraftCockpitFeature.h"
#include "ViewportContext.h"
#include "EarthView.h"
#include "EnvFeatures.h"
#include "EngagementVisualFeature.h"
#include "PlatformAnimationEffects.h"
#include "VoxelSceneFeature.h"
#include "FeatureRegistry.h"
#include "VsgQtWidget.h"
#include "AircraftExterior.h"
#include <simcore/Orientation.h>
#include <vsg/all.h>
#include "base/PlatformPresentation.h"
#include <vsgImGui/imgui.h>
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace vsgraysim::viewport3d {
namespace {
using json=nlohmann::json;
constexpr const char* cockpitEffectOwner="cockpit-ownship";
double value(const json& j,const char* name,double fallback=0){const auto i=j.find(name);return i!=j.end()&&i->is_number()&&std::isfinite(i->get<double>())?i->get<double>():fallback;}
double groundSpeed(const json& s){return std::hypot(s["velocity"][0].get<double>(),s["velocity"][2].get<double>());}
std::string sceneObject(const json& s) {
    const auto object=s.find("scene_object");
    return object!=s.end()&&object->is_string()?object->get<std::string>():std::string{};
}
vsg::dquat attitude(const json& s){return simcore::headingRotation(value(s,"yaw"),value(s,"pitch"),value(s,"roll"));}
}
bool AircraftCockpitFeature::validTelemetry(const json& s) {
    if(!s.is_object())return false;
    for(const auto* k:{"yaw","pitch","roll","speed","sim_time"})if(!s.contains(k)||!s[k].is_number()||!std::isfinite(s[k].get<double>()))return false;
    for(const auto* k:{"position","velocity"}){if(!s.contains(k)||!s[k].is_array()||s[k].size()!=3)return false;for(const auto& v:s[k])if(!v.is_number()||!std::isfinite(v.get<double>()))return false;}
    return true;
}
vsg::LookAt AircraftCockpitFeature::pilotCamera(const simcore::geo::GeoAnchor& anchor,const json& s,double yaw,double pitch,const vsg::dvec3& eyeOffset) {
    if(!validTelemetry(s)||!std::isfinite(yaw)||!std::isfinite(pitch)||!std::isfinite(vsg::length(eyeOffset)))throw std::invalid_argument("invalid cockpit pose");
    const auto& p=s.at("position");const auto body=vsg::translate(p[0].get<double>(),p[1].get<double>(),p[2].get<double>())*vsg::rotate(attitude(s));
    const auto world=simcore::geo::makeEnuToWorldMatrix(anchor)*body;
    const auto look=vsg::rotate(simcore::headingRotation(yaw,pitch,0));
    const auto f=world*(look*vsg::dvec4(0,0,-1,0)),u=world*(look*vsg::dvec4(0,1,0,0));
    const auto eye=world*eyeOffset;return {eye,eye+vsg::dvec3(f.x,f.y,f.z)*1000,vsg::dvec3(u.x,u.y,u.z)};
}
vsg::LookAt AircraftCockpitFeature::chaseCamera(const simcore::geo::GeoAnchor& anchor,const json& s,double distance,double yaw,double pitch){
    if(!validTelemetry(s)||!std::isfinite(distance)||distance<10||distance>1000||!std::isfinite(yaw)||!std::isfinite(pitch))throw std::invalid_argument("invalid exterior camera pose");
    const auto world=simcore::geo::makeEnuToWorldMatrix(anchor);const auto& p=s.at("position");const vsg::dvec3 centre(p[0].get<double>(),p[1].get<double>(),p[2].get<double>());
    const double elevation=vsg::radians(std::clamp(15.+pitch,-60.,75.));const auto orbit=vsg::rotate(simcore::headingRotation(value(s,"yaw")+yaw,0,0));
    const auto eye=centre+orbit*vsg::dvec3(0,std::sin(elevation)*distance,std::cos(elevation)*distance);
    const auto up=world*vsg::dvec4(0,1,0,0);return {world*eye,world*centre,vsg::dvec3(up.x,up.y,up.z)};
}
void AircraftCockpitFeature::onAttach(ViewportContext& ctx) {
    context_=&ctx;
    root_=vsg::Switch::create();
    body_=vsg::MatrixTransform::create();
    root_->addChild(false,body_);
    exteriorGeometry_=vsg::Group::create();
    body_->addChild(exteriorGeometry_);
    interiorDraw_=vsg::Switch::create();
    interiorBody_=vsg::MatrixTransform::create();
    mountedInterior_=vsg::MatrixTransform::create();
    interiorDraw_->addChild(false,interiorBody_);
    interiorBody_->addChild(mountedInterior_);
    root_->addChild(true,interiorDraw_);
    photoResources_=vsg::Group::create();
    root_->addChild(true,photoResources_);
    ctx.worldRoot()->addChild(root_);
    hudLifetime_=std::make_shared<bool>(true);
    hudRegistered_=false;
    ctx.requestCompile();
}
void AircraftCockpitFeature::onDetach(ViewportContext& ctx) {
    if(photoPanel_.ready()&&ctx.viewer())ctx.viewer()->deviceWaitIdle();
    const bool wasEnabled=enabled_;
    setEnabled(false);
    enabled_=wasEnabled;
    context_=nullptr;
    if(hudLifetime_)*hudLifetime_=false;
    if(ctx.widget())ctx.widget()->setNearClipOverride(0);
    ctx.removeFromWorld(root_);
    root_=nullptr;body_=nullptr;exteriorGeometry_=nullptr;
    photoPanel_.reset();photoResources_=nullptr;
    spatial_={};stick_=nullptr;throttle_=nullptr;
    interiorDraw_=nullptr;interiorBody_=nullptr;mountedInterior_=nullptr;
    exteriorReady_=false;modelDirty_=true;interiorDirty_=true;
}
void AircraftCockpitFeature::setEnabled(bool enabled) {
    enabled_=enabled;
    if(enabled)return;
    if(root_)root_->children.front().mask=0;
    if(interiorDraw_)interiorDraw_->setAllChildren(false);
    if(context_) {
        if(auto* fx=context_->featureAs<EngagementVisualFeature>(EngagementVisualFeature::kId))fx->setPlatformPlumes(cockpitEffectOwner,{});
        if(auto* scene=context_->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId))scene->setCockpitOwnship({});
        if(context_->widget())context_->widget()->setNearClipOverride(0);
    }
}
void AircraftCockpitFeature::configure(const json& j){const auto mode=j.value("view_mode",mode_);if(mode!="pilot"&&mode!="overview"&&mode!="external")throw std::invalid_argument("invalid cockpit camera mode");
    sceneExterior_=j.value("scene_exterior",sceneExterior_);
    const double yaw=j.value("look_yaw_deg",lookYaw_),pitch=j.value("look_pitch_deg",lookPitch_),fov=j.value("fov_deg",fov_);const bool hud=j.value("hud",hudEnabled_);
    if(!std::isfinite(yaw)||!std::isfinite(pitch)||!std::isfinite(fov)||std::abs(yaw)>(mode=="external"?180.:120.)||std::abs(pitch)>75||fov<40||fov>90)throw std::invalid_argument("invalid cockpit view angles/FOV");
    const double distance=j.value("chase_distance",chaseDistance_),dimension=j.value("model_max_dimension",modelDimension_),heading=j.value("model_heading_deg",modelHeading_);const auto path=j.value("model_uri",modelPath_),up=j.value("model_up_axis",modelUpAxis_),kind=j.value("model_kind",modelKind_);
    if(!std::isfinite(distance)||distance<10||distance>1000||!std::isfinite(dimension)||dimension<=0||(up!="y"&&up!="z"))throw std::invalid_argument("invalid exterior view parameters");
    if(!std::isfinite(heading))throw std::invalid_argument("invalid exterior heading");modelDirty_|=path!=modelPath_||up!=modelUpAxis_||kind!=modelKind_||dimension!=modelDimension_||heading!=modelHeading_;modelPath_=path;modelUpAxis_=up;modelKind_=kind;modelDimension_=dimension;modelHeading_=heading;chaseDistance_=distance;
    const auto modelId=j.value("model_id",animationId_.empty()&&path.find("/F-16/")!=std::string::npos
                              ?std::string("f16c"):animationId_);
    if(modelId!=animationId_) {
        exteriorAnimation_=platform_animation::makePlatformAnimation(modelId);
        cabinModel_=platform_animation::findCockpitModel(modelId);
        animationId_=modelId;
    }
    const auto category=j.value("cockpit_category",cockpitCategory_);
    if(category!=cockpitCategory_) {
        cockpitCategory_=category;
        interior_=cockpitInterior(category);
        interiorDirty_=true;
    }
    if(j.contains("photo_panel")) {
        photoPanel_.configure(j.at("photo_panel"));
        if(photoPanel_.needsPrepare()) {
            if(!interiorDirty_){pages_={};hover_=CabinAction::None;lightStep_=1;}
            interiorDirty_=true;
        }
    }
    if(mode!=mode_||lookYaw_!=yaw||lookPitch_!=pitch||fov_!=fov)hover_=CabinAction::None;
    mode_=mode;lookYaw_=yaw;lookPitch_=pitch;fov_=fov;hudEnabled_=hud;
}
void AircraftCockpitFeature::setAircraft(const json& j,bool fresh) {
    if(j!=aircraft_)aircraft_=validTelemetry(j)?j:json::object();
    fresh_=fresh;
}
CabinAction AircraftCockpitFeature::cabinPick(double x,double y,double width,double height) const {
    if(!spatialPanelActive()||width<=0||height<=0||x<0||y<0||x>=width||y>=height)return CabinAction::None;
    const double focal=height*.5/std::tan(vsg::radians(fov_*.5));
    const auto view=vsg::rotate(simcore::headingRotation(lookYaw_,lookPitch_));
    const auto ray=view*vsg::dvec3((x-width*.5)/focal,(height*.5-y)/focal,-1);
    return spatial_.pick(vsg::vec3(vsg::normalize(ray)));
}
void AircraftCockpitFeature::selectCabinPage(CabinAction action) {
    switch(action) {
    case CabinAction::LeftFlight:pages_[0]="FLT";break;
    case CabinAction::LeftControl:pages_[0]="CTRL";break;
    case CabinAction::LeftEngine:pages_[0]="ENG";break;
    case CabinAction::RightFlight:pages_[1]="FLT";break;
    case CabinAction::RightControl:pages_[1]="CTRL";break;
    case CabinAction::RightEngine:pages_[1]="ENG";break;
    default:break;
    }
}
void AircraftCockpitFeature::cycleCabinLight(){lightStep_=(lightStep_+1)%3;}

void AircraftCockpitFeature::drawPilotHud() const {
    if(!validTelemetry(aircraft_))return;
    const auto size=ImGui::GetIO().DisplaySize;
    if(size.x<250||size.y<160)return;
    auto* draw=ImGui::GetForegroundDrawList();
    const double focal=size.y*.5/std::tan(vsg::radians(fov_*.5));
    const auto view=vsg::inverse(vsg::rotate(simcore::headingRotation(lookYaw_,lookPitch_)));
    const auto project=[&](const vsg::dvec3& p,ImVec2& screen){
        const auto q=view*p;
        if(q.z>=-.04)return false;
        screen=ImVec2(float(size.x*.5-q.x*focal/q.z),float(size.y*.5+q.y*focal/q.z));
        return true;
    };
    const auto label=[&](ImVec2 centre,const char* text,float px,ImU32 ink) {
        const float width=ImGui::GetFont()->CalcTextSizeA(px,1000.f,0.f,text).x;
        draw->AddText(ImGui::GetFont(),px,{centre.x-width*.5f,centre.y-px*.5f},ink,text);
    };
    const bool spatial=spatialPanelActive();
    const int hudGain=spatial?int(52.f*(1.f-cabinDay_)):0;
    const auto colour=fresh_?IM_COL32(145-hudGain,255-hudGain,174-hudGain,255):IM_COL32(255-hudGain,198-hudGain,114-hudGain,255);
    constexpr std::array<float,3> lamps{0.f,.26f,1.f};
    const float photoTop=photoPanel_.draw(aircraft_,fresh_,lookYaw_,lookPitch_,fov_,spatial,lamps[lightStep_],cabinDay_,pages_);
    if(spatial) {
        const auto fc=aircraft_.find("flight_control");
        const auto systems=fc!=aircraft_.end()&&fc->is_object()?fc->value("systems",json::object()):json::object();
        const bool available=systems.is_object()&&systems.value("available",false);
        const auto state=[&](const char* k){return available&&systems.value(k,false);};
        const double travel=available?value(systems,"gear_extension"):0;
        const bool transit=available&&(state("gear_down")?travel<1:travel>0);
        const float font=std::clamp(float(focal*.019),9.f,14.f);
        const auto atPanel=[&](float u,float v, float depth, const char* title,ImU32 ink) {
            ImVec2 p;
            if(std::abs(lookYaw_)<44&&project(vsg::dvec3(cockpitPanelPoint(u,v))+vsg::dvec3(0,0,depth),p))
                label(p,title,font,ink);
        };
        const auto warm=cabinDay_>.5?IM_COL32(174,187,173,255):IM_COL32(117,137,98,255);
        const auto dim=IM_COL32(112,123,116,255);
        const auto green=IM_COL32(145,218,152,255),amber=IM_COL32(237,172,83,255);
        atPanel(.171f,.505f,.034f,"LDG GEAR",warm);
        atPanel(.172f,.626f,.049f,!available?"NO DATA":transit?"TRANSIT":state("gear_down")?"DOWN / LOCK":"UP / LOCK",
                !available?dim:transit?amber:state("gear_down")?green:dim);
        atPanel(.173f,.354f,.040f,"POS",state("nav_lights")?green:warm);
        atPanel(.173f,.428f,.040f,"LDG",state("landing_light_active")?green:state("landing_light")?amber:warm);
    }
    if(spatial&&hover_!=CabinAction::None)for(const auto& c:fighterCabinControls())if(c.action==hover_) {
        ImVec2 at;
        if(project(vsg::dvec3(c.center),at)&&at.x>0&&at.y>0&&at.x<size.x&&at.y<size.y) {
            const auto width=ImGui::GetFont()->CalcTextSizeA(14,1000,0,c.label).x;
            const ImVec2 p(std::clamp(at.x,width*.5f+9,size.x-width*.5f-9),std::max(17.f,at.y-31.f));
            draw->AddRectFilled({p.x-width*.5f-7,p.y-11},{p.x+width*.5f+7,p.y+11},IM_COL32(9,14,17,228),4);
            label(p,c.label,14,IM_COL32(217,235,208,255));
            draw->AddCircle(at,10,IM_COL32(195,235,178,204),20,1.1f);
        }
        break;
    }
    const auto putDisplay=[&](float x,const char* title,const char* digits) {
        ImVec2 top,reading;
        if(project({x,interior_.brow-.093,-.944},top)&&project({x,interior_.brow-.155,-.944},reading)) {
            const float font=std::clamp(float(focal*.040),8.f,18.f);
            label(top,title,font*.78f,IM_COL32(108,180,168,255));
            label(reading,digits,font,colour);
        }
    };
    if(interior_.interior&&!photoPanel_.ready()) {
        char primary[24],secondary[24];
        std::snprintf(primary,sizeof(primary),"%.0f kt",groundSpeed(aircraft_)*1.943844);
        const double height=value(aircraft_,"altitude_m",aircraft_["position"][1].get<double>());
        std::snprintf(secondary,sizeof(secondary),"%.0f m",height);
        const float x=interior_.widePanel?.42f:.315f;
        // Limit screen glyphs to readable head angles. The 3D bezels remain at any angle.
        if(std::abs(lookYaw_)<40&&std::abs(lookPitch_)<45) {
            putDisplay(-x,"GROUND SPD",primary);
            putDisplay(x,aircraft_.contains("altitude_m")?"ELLIPS. ALT":"LOCAL ALT",secondary);
        }
    }
    if(!hudEnabled_||(spatial&&std::abs(lookYaw_)>53))return;
    const auto glass=[&](double x,double y,ImVec2& at){
        return project({x*interior_.glassHalfWidth,interior_.glassCentreY+y*interior_.glassHalfHeight,-interior_.glassDepth},at);
    };
    ImVec2 tl,tr,bl,br;
    if(!glass(-.92,.91,tl)||!glass(.92,.91,tr)||!glass(-.92,-.91,bl)||!glass(.92,-.91,br))return;
    const ImVec2 lo(std::min({tl.x,tr.x,bl.x,br.x}),std::min({tl.y,tr.y,bl.y,br.y}));
    const ImVec2 hi(std::max({tl.x,tr.x,bl.x,br.x}),std::max({tl.y,tr.y,bl.y,br.y}));
    if(hi.x<=0||hi.y<=0||lo.x>=size.x||lo.y>=size.y)return;
    const float font=std::clamp(float(focal*.054),10.f,23.f);
    const auto text=[&](double x,double y,const char* t,float scale=1.f) {
        ImVec2 at;if(glass(x,y,at)) {label({at.x+1,at.y+1},t,font*scale,IM_COL32(5,20,19,225));label(at,t,font*scale,colour);}
    };
    const auto stroke=[&](double ax,double ay,double bx,double by){
        ImVec2 a,b;if(!glass(ax,ay,a)||!glass(bx,by,b))return;
        draw->AddLine(a,b,IM_COL32(5,20,19,230),3.25f);
        draw->AddLine(a,b,colour,1.55f);
    };
    const float hudBottom=photoTop<0?hi.y:std::min(photoTop,hi.y);
    if(hudBottom<=lo.y)return;
    draw->PushClipRect(lo,{hi.x,hudBottom},true);
    char heading[20],speed[20],altitude[20];
    const int degrees=(int(std::lround(std::remainder(value(aircraft_,"yaw"),360.)))+360)%360;
    std::snprintf(heading,sizeof(heading),"%03d",degrees);
    std::snprintf(speed,sizeof(speed),"%.0f",groundSpeed(aircraft_)*1.943844);
    std::snprintf(altitude,sizeof(altitude),"%.0f",value(aircraft_,"altitude_m",aircraft_["position"][1].get<double>()));
    text(0,.74,heading);
    for(int i=-4;i<=4;++i)stroke(i*.14,.55,i*.14,(i%2==0?.65:.60));
    stroke(0,.53,-.05,.47);stroke(0,.53,.05,.47);
    text(-.72,.16,"GS",.66f);text(-.72,-.02,speed,.87f);
    text(.67,.16,aircraft_.contains("altitude_m")?"ELL m":"LOC m",.65f);text(.67,-.02,altitude,.87f);
    const auto pitch=value(aircraft_,"pitch"),roll=vsg::radians(value(aircraft_,"roll"));
    const double cosine=std::cos(roll),sine=std::sin(roll),aspect=interior_.glassHalfWidth/interior_.glassHalfHeight;
    const auto ladder=[&](double a,double b,double y){
        const double ax=a*cosine-y*sine/aspect,ay=a*sine*aspect+y*cosine;
        const double bx=b*cosine-y*sine/aspect,by=b*sine*aspect+y*cosine;
        if(std::max(std::abs(ax),std::abs(bx))<.48&&std::max(std::abs(ay),std::abs(by))<.45)stroke(ax,ay,bx,by);
    };
    for(int deg=-85;deg<=85;deg+=5)if(std::abs(deg-pitch)<9) {
        const double y=.05+std::tan(vsg::radians(deg-pitch))*interior_.glassDepth/interior_.glassHalfHeight;
        const double width=deg==0?.42:.32;
        ladder(-width,-.12,y);ladder(.12,width,y);
    }
    stroke(-.13,.042,-.055,.042);stroke(.055,.042,.13,.042);
    stroke(-.055,.042,0,-.006);stroke(0,-.006,.055,.042);
    if(!fresh_)text(0,-.72,"DATA HOLD",.67f);
    draw->PopClipRect();
}
void AircraftCockpitFeature::drawHud() const {
    if(!enabled_||mode_=="overview")return;
    const auto domain=gui::platformDomain(aircraft_);
    if(domain==gui::PlatformDomain::Air) {
        if(mode_=="pilot")drawPilotHud();
        return;
    }
    if(!hudEnabled_)return;
    const auto size=ImGui::GetIO().DisplaySize;
    if(size.x<250||size.y<160)return;
    auto* draw=ImGui::GetForegroundDrawList();
    const auto colour=fresh_?IM_COL32(137,255,181,255):IM_COL32(255,199,117,255);
    const auto shadow=IM_COL32(5,22,23,225);
    const auto panel=IM_COL32(6,19,24,190);
    const float font=std::clamp(size.y*.030f,13.f,22.f);
    const auto label=[&](ImVec2 at,const char* text,float px) {
        const auto width=ImGui::GetFont()->CalcTextSizeA(px,1000.f,0.f,text).x;
        const ImVec2 start(at.x-width*.5f,at.y);
        draw->AddText(ImGui::GetFont(),px,ImVec2(start.x+1,start.y+1),shadow,text);
        draw->AddText(ImGui::GetFont(),px,start,colour,text);
    };
    const auto line=[&](ImVec2 a,ImVec2 b,float thickness=1.35f){
        draw->AddLine(a,b,shadow,thickness+1.7f);draw->AddLine(a,b,colour,thickness);
    };
    const ImVec2 c(size.x*.5f,size.y*.50f);
    if(!validTelemetry(aircraft_)) {
        label({c.x,28},"WAITING FOR PLATFORM TELEMETRY",font);
        return;
    }
    const bool ground=domain==gui::PlatformDomain::Ground;
    const auto course=value(aircraft_,"yaw");
    const auto hdg=[](double angle){return (int(std::lround(std::remainder(angle,360.)))+360)%360;};
    const float tapeHalf=std::min(size.x*.32f,240.f), tapeY=std::max(33.f,size.y*.113f);
    draw->AddRectFilled({c.x-tapeHalf-8,tapeY-font-9},{c.x+tapeHalf+8,tapeY+18},panel,4);
    draw->PushClipRect({c.x-tapeHalf,tapeY-font-7},{c.x+tapeHalf,tapeY+19},true);
    const double headingBase=std::round(std::remainder(course,360.)/5.)*5.;
    const double centered=std::remainder(course,360.);
    for(int d=-35;d<=35;d+=5){
        const double mark=headingBase+d;
        const float x=c.x+float(mark-centered)*tapeHalf/32.f;
        const int heading=hdg(mark);
        const bool major=heading%10==0;
        line({x,tapeY-(major?12.f:6.f)},{x,tapeY});
        if(major){char tick[12];std::snprintf(tick,sizeof(tick),"%03d",heading);label({x,tapeY-font-13},tick,font*.76f);}
    }
    draw->PopClipRect();
    line({c.x,tapeY},{c.x-6,tapeY+10});line({c.x,tapeY},{c.x+6,tapeY+10});

    char primary[32], secondary[32], state[90];
    std::snprintf(primary,sizeof(primary),"%.1f",groundSpeed(aircraft_)*(ground?3.6:1.943844));
    std::snprintf(secondary,sizeof(secondary),"%+.1f",value(aircraft_,"roll"));
    std::snprintf(state,sizeof(state),"GRID %03d   %s   t=%.2f s",hdg(course),fresh_?"TELEMETRY":"DATA HOLD",value(aircraft_,"sim_time"));
    const auto readout=[&](float x,const char* title,const char* numeric){
        const float half=std::max(39.f,font*3.2f);
        const ImVec2 lo(x-half,c.y-font*1.5f),hi(x+half,c.y+font*1.42f);
        draw->AddRectFilled(lo,hi,panel,4);draw->AddRect(lo,hi,colour,4);
        label({x,lo.y+3},title,font*.76f);label({x,c.y-font*.35f},numeric,font*1.20f);
    };
    const float lateral=std::min(size.x*.34f,std::max(80.f,size.y*.59f));
    readout(c.x-lateral,ground?"SPEED km/h":"SOG kt",primary);
    readout(c.x+lateral,"ROLL deg",secondary);
    label({c.x,size.y-font*2.05f},state,font*.88f);
    char angles[80];
    std::snprintf(angles,sizeof(angles),"PITCH %+.1f   ROLL %+.1f",value(aircraft_,"pitch"),value(aircraft_,"roll"));
    label({c.x,size.y-font*3.35f},angles,font*.83f);
}
std::optional<simcore::geo::GeoAnchor> AircraftCockpitFeature::referenceAnchor(ViewportContext& ctx) const {
    // Explicit voxel telemetry is relative to the simulation scene chart.
    if(const auto id=sceneObject(aircraft_); !id.empty()) {
        if(const auto* scene=ctx.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId))
            if(const auto state=scene->snapshot(); state&&state->objects.count(id))return state->anchor;
    }
    if(const auto* earth=ctx.featureAs<EarthView>(EarthView::kId))return earth->anchor();
    return std::nullopt;
}
void AircraftCockpitFeature::synchronizeCamera(ViewportContext& ctx) {
    const bool follow=enabled_&&mode_!="overview"&&validTelemetry(aircraft_);
    const bool pilot=follow&&mode_=="pilot";
    if(ctx.widget())ctx.widget()->setNearClipOverride(pilot?.04:0);
    if(auto* scene=ctx.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId))
        scene->setCockpitOwnship(pilot?sceneObject(aircraft_):std::string{});
    if(!follow)return;
    const auto anchor=referenceAnchor(ctx);
    auto* cam=ctx.camera();
    if(!anchor||!cam)return;
    const auto look=pilot?pilotCamera(*anchor,aircraft_,lookYaw_,lookPitch_,interior_.eye):
        chaseCamera(*anchor,aircraft_,chaseDistance_,lookYaw_,lookPitch_);
    if(auto target=cam->viewMatrix.cast<vsg::LookAt>()) {
        target->eye=look.eye;target->center=look.center;target->up=look.up;
    }
    if(auto perspective=cam->projectionMatrix.cast<vsg::Perspective>()) {
        perspective->fieldOfViewY=fov_;perspective->nearDistance=.04;perspective->farDistance=5e7;
    }
}
void AircraftCockpitFeature::onFrame(ViewportContext& ctx,const FrameTick& tick) {
    if(!body_)return;
    if(!hudRegistered_&&ctx.widget()&&ctx.widget()->imGuiReady()) {
        hudRegistered_=true;
        ctx.widget()->addImGuiDraw([this,lifetime=hudLifetime_] {
            if(!*lifetime)return false;
            drawHud();
            return true;
        });
    }
    const bool valid=validTelemetry(aircraft_);
    const bool drawExterior=enabled_&&valid&&mode_=="external"&&!sceneExterior_&&
        gui::platformDomain(aircraft_)==gui::PlatformDomain::Air;
    root_->children.front().mask=drawExterior?~vsg::Mask(0):vsg::Mask(0);
    const bool pilot=enabled_&&valid&&mode_=="pilot"&&gui::platformDomain(aircraft_)==gui::PlatformDomain::Air;
    if(pilot&&ctx.window()&&ctx.widget()&&ctx.widget()->imGuiReady()&&photoPanel_.needsPrepare()) {
        // ImGui descriptor sets can still be referenced by submitted frames.
        if(!photoResources_->children.empty()&&ctx.viewer())ctx.viewer()->deviceWaitIdle();
        if(photoPanel_.prepare(*photoResources_,ctx.window()->getOrCreateDevice()->deviceID))ctx.requestCompile();
    }
    const bool useSpatial=photoPanel_.spatialRequested()&&photoPanel_.ready();
    if(pilot&&interiorDirty_&&interior_.interior) {
        if(!mountedInterior_->children.empty()&&ctx.viewer())ctx.viewer()->deviceWaitIdle();
        spatial_={};stick_=nullptr;throttle_=nullptr;
        mountedInterior_->children.clear();
        mountedInterior_->matrix=vsg::translate(interior_.eye);
        if(useSpatial)spatial_=makeFighterCabin(interior_,photoPanel_.artwork(),stick_,throttle_);
        if(spatial_.node)mountedInterior_->addChild(spatial_.node);
        else if(!photoPanel_.ready())if(auto mesh=makeCockpitInterior(interior_))mountedInterior_->addChild(mesh);
        interiorDirty_=false;
        ctx.requestCompile();
    }
    const bool drawInterior=pilot&&interior_.interior&&(!photoPanel_.ready()||spatial_.node);
    interiorDraw_->setAllChildren(drawInterior);
    if(drawExterior&&modelDirty_) {
        auto model=loadAircraftExterior(modelPath_,modelUpAxis_,modelKind_,modelDimension_,ctx.options(),representative_,modelHeading_);
        if(!exteriorGeometry_->children.empty()&&ctx.viewer())ctx.viewer()->deviceWaitIdle();
        exteriorGeometry_->children.clear();
        if(model)exteriorGeometry_->addChild(model);
        exteriorReady_=bool(model);modelDirty_=false;ctx.requestCompile();
    }
    if(valid)if(const auto anchor=referenceAnchor(ctx)) {
        const auto& p=aircraft_["position"];
        body_->matrix=simcore::geo::makeEnuToWorldMatrix(*anchor)*
            vsg::translate(p[0].get<double>(),p[1].get<double>(),p[2].get<double>())*vsg::rotate(attitude(aircraft_));
        interiorBody_->matrix=body_->matrix;
        if(pilot&&spatial_.node) {
            constexpr std::array<float,3> lamps{0.f,.26f,1.f};
            if(auto* sky=ctx.featureAs<SkyFeature>(SkyFeature::kId);sky&&sky->enabled()) {
                rockyenv::AtmosphereState light;
                if(auto* provider=sky->lightingProvider();provider&&provider->getAtmosphereState(light)) {
                    auto toLocal=[&](vsg::vec3 world) {
                        const auto dir=vsg::inverse(body_->matrix)*vsg::dvec4(world.x,world.y,world.z,0.);
                        return vsg::vec3(dir.x,dir.y,dir.z);
                    };
                    auto localSun=vsg::vec3(vsg::inverse(vsg::rotate(attitude(aircraft_)))*vsg::dvec3(light.sun_dir_enu));
                    if(auto* sun=sky->sunProvider()) {
                        int y,m,d;double hours;
                        sky->currentDateTime(tick,y,m,d,hours);
                        sun->syncDateTime(y,m,d,hours);
                        rockyenv::EnvSunInfo info;
                        if(sun->sunInfo(simcore::geo::makeEnuToWorldMatrix(*anchor),info))localSun=toLocal(info.dirECEF);
                        if(!sun->solarEnabled())light.sun_light={0,0,0};
                    }
                    cabinDay_=std::clamp(light.is_day,0.f,1.f);
                    spatial_.updateLight(light.ambient_light+light.moon_light*.35f,light.sun_light,localSun,lamps[lightStep_]);
                }
            }
            const auto control=aircraft_.find("flight_control");
            const auto* actual=control!=aircraft_.end()&&control->is_object()&&control->contains("actual")&&control->at("actual").is_object()
                ?&control->at("actual"):nullptr;
            const bool hold=control!=aircraft_.end()&&control->is_object()&&control->value("mode",std::string{})=="altitude_hold";
            const auto moveSwitch=[&](const auto& tr,size_t index,double angle,bool knob) {
                if(!tr)return;
                const auto p=vsg::dvec3(fighterCabinControls()[index].center);
                tr->matrix=vsg::translate(p)*(knob?vsg::rotate(angle,0.,0.,1.):vsg::rotate(angle,1.,0.,0.));
            };
            const auto systems=control!=aircraft_.end()&&control->is_object()?control->value("systems",json::object()):json::object();
            const auto selected=[&](const char* name) {
                const auto field=systems.find(name);
                return field!=systems.end()&&field->is_boolean()&&field->get<bool>();
            };
            if(cabinModel_) {
                const auto axis=[&](const char* name,double fallback=0.) {
                    return actual?value(*actual,name,fallback):fallback;
                };
                const auto cabin=cabinModel_->pose({axis("throttle",.5),axis("elevator"),axis("aileron"),
                    lightStep_,hudEnabled_,hold,selected("gear_down"),selected("nav_lights"),selected("landing_light")});
                moveSwitch(spatial_.gearHandle,9,cabin.gear,false);
                moveSwitch(spatial_.navToggle,10,cabin.navLight,false);
                moveSwitch(spatial_.landingToggle,11,cabin.landingLight,false);
                moveSwitch(spatial_.lightKnob,6,cabin.lamp,true);
                moveSwitch(spatial_.hudToggle,7,cabin.hud,false);
                moveSwitch(spatial_.holdToggle,8,cabin.hold,false);
                if(stick_)stick_->matrix=vsg::translate(.59,-.56,-.33)*
                    vsg::rotate(cabin.stickRoll,0.,0.,-1.)*vsg::rotate(cabin.stickPitch,1.,0.,0.);
                if(throttle_)throttle_->matrix=vsg::translate(-.60,-.55,-.33+cabin.throttleOffset);
            }
        }
    }
    if(auto* fx=ctx.featureAs<EngagementVisualFeature>(EngagementVisualFeature::kId)) {
        if(drawExterior&&exteriorReady_&&!representative_&&exteriorAnimation_&&aircraft_.value("alive",true)) {
            const auto fc=aircraft_.find("flight_control");
            const auto* actual=fc!=aircraft_.end()&&fc->is_object()&&fc->contains("actual")&&fc->at("actual").is_object()
                ?&fc->at("actual"):nullptr;
            const double throttle=actual?value(*actual,"throttle"):0.;
            exteriorAnimation_->sample({tick.elapsed_sec,tick.elapsed_sec,std::nullopt,throttle},exteriorFrame_);
            publishAnimationEffects(*fx,cockpitEffectOwner,exteriorFrame_,[&](std::string_view attachment)->std::optional<vsg::dmat4> {
                const auto offset=exteriorAnimation_->modelAttachment(attachment,modelDimension_);
                if(!offset)return {};
                return vsg::inverse(ctx.contentModel())*body_->matrix*vsg::translate((*offset)[0],(*offset)[1],(*offset)[2]);
            });
        } else fx->setPlatformPlumes(cockpitEffectOwner,{});
    }
    synchronizeCamera(ctx);
}
VSGVP_REGISTER_FEATURE(AircraftCockpitFeature);
}
