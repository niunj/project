#pragma once
#include "CockpitInterior.h"
#include "platform_animation/CockpitModel.h"
#include <vsg/core/Array.h>
#include <vsg/maths/vec4.h>
#include <array>
#include <vector>

namespace vsg { class Data; class MatrixTransform; }
namespace vsgraysim::viewport3d {
using platform_animation::CabinAction;
struct CabinControl {
    CabinAction action;
    vsg::vec3 center;
    vsg::vec3 normal;
    float radius;
    const char* label;
};
const std::array<CabinControl,12>& fighterCabinControls();
struct SpatialCockpit {
    vsg::ref_ptr<vsg::Node> node;
    vsg::ref_ptr<vsg::vec4Array> light;
    vsg::ref_ptr<vsg::MatrixTransform> lightKnob,hudToggle,holdToggle,gearHandle,navToggle,landingToggle;
    std::vector<vsg::vec3> occluders; // triangle positions in eye coordinates
    void updateLight(vsg::vec3 ambient,vsg::vec3 sun,vsg::vec3 localSun,float panelLamp) const;
    CabinAction pick(vsg::vec3 ray) const;
    size_t triangleCount=0;
};
vsg::vec3 cockpitPanelPoint(float u,float v);
SpatialCockpit makeFighterCabin(const CockpitInterior&,vsg::ref_ptr<vsg::Data> artwork,
                               vsg::ref_ptr<vsg::MatrixTransform>& stick,
                               vsg::ref_ptr<vsg::MatrixTransform>& throttle);
}
