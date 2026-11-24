#pragma once
#include "IViewportFeature.h"
#include <nlohmann/json.hpp>
#include <simcore/GeoFrame.h>
#include <vsg/app/ViewMatrix.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/Switch.h>
#include <vsg/core/Array2D.h>
#include <QImage>
namespace vsgraysim::viewport3d {
/// Display-only cockpit: consumes authoritative flight telemetry; never
/// integrates a second flight model or changes terrain/cloud/voxel state.
class AircraftCockpitFeature final : public IViewportFeature {
public:
    static constexpr const char* kId="aircraft_cockpit";
    const char* id()const override{return kId;}
    void onAttach(ViewportContext&)override;
    void onDetach(ViewportContext&)override;
    void onFrame(ViewportContext&,const FrameTick&)override;
    void configure(const nlohmann::json&)override;
    void setAircraft(const nlohmann::json&,bool fresh);
    void synchronizeCamera(ViewportContext&);
    static vsg::LookAt pilotCamera(const simcore::geo::GeoAnchor&,const nlohmann::json&,double lookYaw=0,double lookPitch=0);
    static vsg::LookAt chaseCamera(const simcore::geo::GeoAnchor&,const nlohmann::json&,double distance,double orbitYaw=0,double orbitPitch=0);
    bool exteriorReady()const{return exteriorReady_;}
    bool representativeExterior()const{return representative_;}
    vsg::dmat4 displayPoseMatrix()const{return body_?body_->matrix:vsg::dmat4(1);}
    static QImage instruments(const nlohmann::json&,bool fresh);
    static QImage hud(const nlohmann::json&,bool fresh);
    static bool validTelemetry(const nlohmann::json&);
private:
    nlohmann::json aircraft_=nlohmann::json::object();
    bool fresh_=false,dirty_=true,hudEnabled_=true,interiorEnabled_=true;
    std::string mode_="pilot";
    double lookYaw_=0,lookPitch_=0,fov_=65;
    double chaseDistance_=65,modelDimension_=20,modelHeading_=0;
    std::string modelPath_,modelUpAxis_="z",modelKind_="aircraft";
    bool modelDirty_=true,representative_=false,exteriorReady_=false;
    bool sceneExterior_=false;
    vsg::ref_ptr<vsg::MatrixTransform> body_;
    vsg::ref_ptr<vsg::Switch> root_,hud_,interior_,exterior_;
    vsg::ref_ptr<vsg::Group> exteriorGeometry_;
    vsg::ref_ptr<vsg::ubvec4Array2D> instrumentImage_,hudImage_;
};
}
