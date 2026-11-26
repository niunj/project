#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <string>
#include <vsg/core/ref_ptr.h>
#include <vsgImGui/Texture.h>

namespace vsg { class Group; }
namespace vsgraysim::viewport3d {
// Seat view instrument textures. The live scene and all telemetry are independent of this artwork.
class CockpitPhotoPanel {
public:
    void configure(const nlohmann::json&);
    bool prepare(vsg::Group&,uint32_t deviceId); // GUI thread, before graph compilation
    void reset();
    bool needsPrepare() const {return dirty_;}
    bool ready() const {return bool(background_);}
    bool spatialRequested() const;
    vsg::ref_ptr<vsg::Data> artwork() const {return artwork_;}
    float draw(const nlohmann::json& telemetry,bool fresh,double headYaw,double headPitch,double fov,bool spatial,float light,float daylight,const std::array<std::string,2>& pages) const;
private:
    nlohmann::json layout_;
    vsg::ref_ptr<vsgImGui::Texture> background_,grips_;
    vsg::ref_ptr<vsg::Data> artwork_;
    uint32_t deviceId_=0;
    bool dirty_=true;
};
}
