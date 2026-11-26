#include "CockpitPhotoPanel.h"
#include "CockpitSpatial.h"
#include <simcore/Orientation.h>
#include <QImageReader>
#include <QString>
#include <spdlog/spdlog.h>
#include <vsg/all.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>

namespace vsgraysim::viewport3d {
namespace {
using json=nlohmann::json;
constexpr double radians=.017453292519943295;
vsg::ref_ptr<vsg::Data> load(const std::string& path,bool feather) {
    if(path.empty())return {};
    QImageReader reader(QString::fromStdString(path));
    const auto size=reader.size();
    if(!size.isValid()||size.width()>4096||size.height()>4096) {
        spdlog::warn("Cockpit artwork unavailable or exceeds 4096 pixels: {}",path); return {};
    }
    const auto image=reader.read().convertToFormat(QImage::Format_RGBA8888);
    if(image.isNull()) {spdlog::warn("Cannot decode cockpit artwork: {}",path); return {};}
    auto rgba=vsg::ubvec4Array2D::create(image.width(),image.height(),vsg::Data::Properties{VK_FORMAT_R8G8B8A8_SRGB});
    for(int y=0;y<image.height();++y) {
        auto* row=rgba->data()+y*image.width();
        std::memcpy(row,image.constScanLine(y),size_t(image.width())*4);
        if(feather)for(int x=0;x<image.width();++x) {
            const auto smooth=[](double t){t=std::clamp(t,0.,1.);return t*t*(3.-2.*t);};
            row[x].a=uint8_t(row[x].a*smooth(std::min(x,image.width()-1-x)/(image.width()*.026)) * smooth(y/(image.height()*.017)));
        }
    }
    return rgba;
}
std::optional<double> measure(const json& root,const std::string& key) {
    if(key=="ground_speed_kt") {
        const auto x=measure(root,"velocity.0"),z=measure(root,"velocity.2");
        if(x&&z)return std::hypot(*x,*z)*1.943844;
        return {};
    }
    const auto* value=&root;
    for(size_t start=0;start<key.size();) {
        const auto end=key.find('.',start);const auto field=key.substr(start,end-start);
        if(value->is_array()) {
            size_t index=0;const auto last=field.data()+field.size();const auto [ptr,error]=std::from_chars(field.data(),last,index);
            if(error!=std::errc{}||ptr!=last||index>=value->size())return {};
            value=&value->at(index);
        } else if(value->is_object()) {auto i=value->find(field);if(i==value->end())return {};value=&*i;}
        else return {};
        if(end==std::string::npos)break;
        start=end+1;
    }
    if(value->is_number()&&std::isfinite(value->get<double>()))return value->get<double>();
    return {};
}
struct Rect { ImVec2 a,b; float width()const{return b.x-a.x;}float height()const{return b.y-a.y;}
    ImVec2 point(float u,float v)const{return {a.x+u*width(),a.y+v*height()};}
    Rect relative(const json& r)const {const auto q=r.get<std::array<float,4>>();return {point(q[0],q[1]),point(q[0]+q[2],q[1]+q[3])};}
};
std::string valueText(std::optional<double> value,int precision=0) {
    if(!value)return "---";
    char s[48]; std::snprintf(s,sizeof(s),"%.*f",precision,*value);return s;
}
}
void CockpitPhotoPanel::configure(const json& layout){if(layout!=layout_){layout_=layout;dirty_=true;}}
void CockpitPhotoPanel::reset(){background_=nullptr;grips_=nullptr;artwork_=nullptr;dirty_=true;}
bool CockpitPhotoPanel::prepare(vsg::Group& graph,uint32_t deviceId) {
    if(!dirty_)return false;
    graph.children.clear();reset();dirty_=false;deviceId_=deviceId;
    if(!layout_.is_object())return true;
    if(auto pixels=load(layout_.value("background",std::string{}),true)) {
        artwork_=pixels;
        background_=vsgImGui::Texture::create(pixels);
        graph.addChild(background_);
        if(auto gripPixels=load(layout_.value("grips",std::string{}),false)) {
            grips_=vsgImGui::Texture::create(gripPixels);
            graph.addChild(grips_);
        }
    }
    return true;
}
bool CockpitPhotoPanel::spatialRequested() const {
    return layout_.is_object()&&layout_.value("mount",std::string{})=="fighter";
}
float CockpitPhotoPanel::draw(const json& sample,bool fresh,double headYaw,double headPitch,double fov,bool spatial,float lightLevel,float daylight,const std::array<std::string,2>& pages) const {
    if(!ready()||(spatial&&std::abs(headYaw)>90))return -1;
    const auto size=ImGui::GetIO().DisplaySize;
    if(size.x<250||size.y<160)return -1;
    const auto alpha=spatial?1.:std::clamp((63.-std::abs(headYaw))/30.,0.,1.);
    if(alpha<=0.)return -1;
    const float aspect=float(background_->width)/float(background_->height);
    const float w=spatial?std::min(size.x,size.y*aspect):std::min(size.x*.97f,size.y*.72f*aspect);
    const float h=w/aspect;
    const float x=spatial?0.f:size.x*.5f-w*.5f-float(std::sin(headYaw*radians))*w*.51f;
    const float top=spatial?0.f:size.y-h*.75f+float(headPitch)*size.y*.009f;
    Rect panel{{x,top},{x+w,top+h}};
    if(top>=size.y)return -1;
    const auto head=vsg::inverse(vsg::rotate(simcore::headingRotation(headYaw,headPitch)));
    const float focal=float(size.y*.5/std::tan(fov*radians*.5));
    const auto cameraPoint=[&](ImVec2 p) {
        return head*vsg::dvec3(cockpitPanelPoint((p.x-x)/w,(p.y-top)/h));
    };
    const auto project=[&](const vsg::dvec3& q) {
        return ImVec2(size.x*.5f-float(q.x/q.z)*focal,size.y*.5f+float(q.y/q.z)*focal);
    };
    auto* draw=ImGui::GetForegroundDrawList();
    const auto ink=[&](int r,int g,int b){return IM_COL32(r,g,b,int(255*alpha));};
    const auto lit=[&](int r,int g,int b) {
        const float nightGain=.39f+.40f*std::sqrt(lightLevel);
        const float k=spatial?nightGain+(1.f-nightGain)*daylight:1.f;
        return ink(int(r*k),int(g*k),int(b*k));
    };
    const auto green=lit(171,235,184), light=lit(213,230,222), yellow=lit(255,197,116), shade=ink(1,3,4);
    const auto text=[&](ImVec2 p,const std::string& s,float px,ImU32 color){
        const auto font=ImGui::GetFont();const auto bounds=font->CalcTextSizeA(px,1000,0,s.c_str());
        draw->AddText(font,px,{p.x-bounds.x*.5f,p.y-bounds.y*.5f},color,s.c_str());
    };
    if(!spatial)draw->AddImage(background_->id(deviceId_),panel.a,panel.b,{0,0},{1,1},ink(255,255,255));
    for(const auto& item:layout_.at("instruments")) {
        const auto r=panel.relative(item.at("rect"));const auto kind=item.at("kind").get<std::string>();
        if(spatial) {
            const auto nearEnough=[&](ImVec2 p){return cameraPoint(p).z<-.09;};
            if(!nearEnough(r.a)||!nearEnough(r.b)||!nearEnough({r.a.x,r.b.y})||!nearEnough({r.b.x,r.a.y}))continue;
        }
        const auto c=r.point(.5f,.5f);const float px=spatial?r.height()*.138f:std::clamp(r.height()*.122f,7.f,19.f);
        const int firstVertex=draw->VtxBuffer.Size;
        if(!spatial)draw->PushClipRect(r.a,r.b,true);
        if(kind=="dial"||kind=="attitude"||kind=="compass") {
            const float radius=std::min(r.width(),r.height())*.46f;
            const auto binding=item.value("binding",std::string{});
            const auto input=measure(sample,kind=="attitude"?"pitch":kind=="compass"?"yaw":binding);
            const auto secondary=kind=="attitude"?measure(sample,"roll"):std::optional<double>{};
            draw->AddCircleFilled(c,radius,shade);
            if(kind=="attitude"&&input&&secondary) {
                constexpr double pi=3.141592653589793;
                const auto bank=-*secondary*radians;
                const auto chord=std::asin(std::clamp(*input/33.,-.85,.85));
                // Adjacent clockwise arcs close along one common horizon chord.
                for(bool sky:{true,false}) {
                    const double start=sky?pi-chord:chord;
                    const double end=sky?2*pi+chord:pi-chord;
                    for(int k=0;k<=24;++k) {
                        const double angle=bank+start+(end-start)*k/24.;
                        draw->PathLineTo({c.x+radius*std::cos(float(angle)),c.y+radius*std::sin(float(angle))});
                    }
                    draw->PathFillConvex(sky?lit(51,104,135):lit(107,77,56));
                }
                draw->AddLine({c.x-radius*.47f,c.y},{c.x+radius*.47f,c.y},light,1.5f);
            } else if(input) {
                const auto min=item.value("min",0.),max=item.value("max",100.);
                const double normalized=std::clamp((*input*item.value("scale",1.)-min)/(max-min),0.,1.);
                const double angle=(kind=="compass"?*input-90.:-225.+270.*normalized)*radians;
                draw->AddLine(c,{c.x+radius*.74f*std::cos(float(angle)),c.y+radius*.74f*std::sin(float(angle))},light,1.5f);
            } else text(c,"INOP",std::clamp(radius*.39f,6.f,13.f),yellow);
            for(int k=0;k<12;++k){const auto a=k*30*radians;const auto dx=float(std::cos(a)),dy=float(std::sin(a));
                draw->AddLine({c.x+dx*radius*.81f,c.y+dy*radius*.81f},{c.x+dx*radius*.91f,c.y+dy*radius*.91f},light);}
        } else {
            draw->AddRectFilled(r.a,r.b,shade,2);
            if(kind=="ded") {
                const auto heading=measure(sample,"yaw");
                text(c,"HDG "+valueText(heading)+"   "+(fresh?"LIVE":"HOLD"),std::min(px*1.36f,r.width()*.055f),heading?green:yellow);
            } else if(kind=="payload") {
                text(c,"EO / IR IN QT CONSOLE",std::min(px,r.width()*.075f),light);
            } else {
                auto page=item.value("page",std::string{});
                if(spatial&&kind=="mfd") {
                    const auto id=item.value("id",std::string{});
                    const size_t display=id=="right_mfd"?1:0;
                    if(!pages[display].empty())page=pages[display];
                }
                if(page.empty())page="FLT";
                text(r.point(.5f,.14f),kind=="pfd"?"PFD":page,px,green);
                const auto row=[&](float y,const char* label,const std::string& key,int precision=0) {
                    const auto v=measure(sample,key);
                    text(r.point(.5f,y),std::string(label)+"  "+valueText(v,precision),std::min(px,r.width()*.071f),v?light:yellow);
                };
                if(page=="CTRL") {
                    row(.36f,"THR","flight_control.actual.throttle",2);row(.56f,"PITCH","flight_control.actual.elevator",2);
                    row(.76f,"ROLL","flight_control.actual.aileron",2);
                    if(spatial) {
                        const auto it=sample.find("flight_control");
                        if(it!=sample.end()&&it->is_object()) {
                            const auto flight=it->value("mode",std::string{});
                            if(!flight.empty())text(r.point(.5f,.91f),flight=="altitude_hold"?"HOLD ACTIVE":flight=="manual"?"MANUAL":"OBSERVE",px*.64f,green);
                        }
                    }
                }
                else if(page=="ENG") {
                    const bool multi=layout_.value("engine_count",1)>1;
                    row(.38f,multi?"ENG 1 %":"RPM %",multi?"engines.0.rpm_pct":"engine.rpm_pct");
                    if(multi)row(.62f,"ENG 2 %","engines.1.rpm_pct");
                }
                else if(page=="NAV") {row(.4f,"HDG","yaw");row(.63f,"LAT","latitude",2);row(.83f,"LON","longitude",2);}
                else if(page=="SYS") {row(.40f,"CLOCK s","sim_time",1);row(.66f,"ROLL deg","roll",1);}
                else if(spatial) {
                    row(.37f,"GS  KT","ground_speed_kt");
                    row(.62f,"LOC  M","position.1");
                    row(.82f,"HDG","yaw");
                    draw->AddLine(r.point(.08f,.245f),r.point(.92f,.245f),green,1.4f);
                }
                else {row(.39f,"GS kt","ground_speed_kt");row(.63f,"LOC m","position.1");row(.82f,"HDG","yaw");}
            }
        }
        if(spatial)for(int i=firstVertex;i<draw->VtxBuffer.Size;++i) {
            auto& p=draw->VtxBuffer[i].pos;
            p=project(cameraPoint(p));
        }
        else draw->PopClipRect();
    }
    if(grips_&&!spatial)for(bool stick:{false,true}) {
        auto rect=panel.relative(layout_.at(stick?"stick":"throttle"));
        const auto actual=[&](const char* channel){return measure(sample,std::string("flight_control.actual.")+channel);};
        const float dx=stick?float(std::clamp(actual("aileron").value_or(0.),-1.,1.)*rect.width()*layout_.at("stick_travel_x").get<double>()):0;
        const float dy=stick?float(std::clamp(actual("elevator").value_or(0.),-1.,1.)*rect.height()*layout_.at("stick_travel_y").get<double>()):
            float((.5-std::clamp(actual("throttle").value_or(.5),0.,1.))*rect.height()*layout_.at("throttle_travel").get<double>());
        draw->AddImage(grips_->id(deviceId_),{rect.a.x+dx,rect.a.y+dy},{rect.b.x+dx,rect.b.y+dy},
                       {stick?.5f:0.f,0},{stick?1.f:.5f,1},ink(255,255,255));
    }
    return spatial?-1.f:top;
}
}
