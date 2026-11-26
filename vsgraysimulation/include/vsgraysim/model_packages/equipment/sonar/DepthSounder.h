#pragma once

#include <vsgraysim/broker/EquipmentInfo.h>
#include <vsgraysim/broker/SonarFrame.h>
#include <vsgraysim/earth/VoxelScene.h>

namespace vsgraysim::model_packages {

struct DepthSounder {
    static constexpr std::string_view id="single-beam-depth-sounder";
    EquipmentInfo equipment;
    std::string hostObject, seabedObject;
    vsg::dvec3 localPosition{};
    double periodSeconds=.4, rangeMetres=150.;
    bool waterColumnReturns=false;

    std::optional<vsg::dvec3> origin(const earth::VoxelSceneSnapshot&) const;
    SonarFrame sample(const earth::VoxelSceneSnapshot&, const vsg::dvec3& transducer) const;
};

}
