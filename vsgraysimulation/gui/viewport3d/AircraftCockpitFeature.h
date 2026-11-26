#pragma once
#include "IViewportFeature.h"
#include "CockpitInterior.h"
#include "CockpitPhotoPanel.h"
#include "CockpitSpatial.h"
#include "platform_animation/PlatformAnimation.h"
#include <nlohmann/json.hpp>
#include <simcore/GeoFrame.h>
#include <vsg/app/ViewMatrix.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/Switch.h>
#include <memory>
#include <optional>
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
    void setEnabled(bool) override;
    void configure(const nlohmann::json&)override;
    void setAircraft(const nlohmann::json&,bool fresh);
    void synchronizeCamera(ViewportContext&);
    static vsg::LookAt pilotCamera(const simcore::geo::GeoAnchor&,const nlohmann::json&,double lookYaw=0,double lookPitch=0,
                                   const vsg::dvec3& eyeOffset={0,1.2,0});
    static vsg::LookAt chaseCamera(const simcore::geo::GeoAnchor&,const nlohmann::json&,double distance,double orbitYaw=0,double orbitPitch=0);
    bool exteriorReady()const{return exteriorReady_&&root_&&root_->children.front().mask!=0;}
    bool interiorVisible()const{return interiorDraw_&&interiorDraw_->children.front().mask!=0&&mountedInterior_&&
        !mountedInterior_->children.empty();}
    bool photoPanelActive() const {return enabled_&&mode_=="pilot"&&photoPanel_.ready()&&validTelemetry(aircraft_);}
    bool spatialPanelActive() const {return photoPanelActive()&&interiorVisible()&&bool(spatial_.node);}
    CabinAction cabinPick(double x,double y,double width,double height) const;
    void setCabinHover(CabinAction action) { hover_=action; }
    void selectCabinPage(CabinAction);
    void cycleCabinLight();
    std::optional<vsg::dmat4> cabinStickTransform() const {
        return stick_&&spatialPanelActive()?std::optional<vsg::dmat4>(stick_->matrix):std::nullopt;
    }
    size_t cabinTriangles() const {return spatial_.triangleCount;}
    bool representativeExterior()const{return representative_;}
    vsg::dmat4 displayPoseMatrix()const{return body_?body_->matrix:vsg::dmat4(1);}
    static bool validTelemetry(const nlohmann::json&);
private:
    void drawHud() const;
    void drawPilotHud() const;
    std::optional<simcore::geo::GeoAnchor> referenceAnchor(ViewportContext&) const;
    ViewportContext* context_=nullptr;
    nlohmann::json aircraft_=nlohmann::json::object();
    bool fresh_=false,hudEnabled_=true,hudRegistered_=false;
    std::shared_ptr<bool> hudLifetime_;
    std::string mode_="pilot";
    double lookYaw_=0,lookPitch_=0,fov_=65;
    double chaseDistance_=65,modelDimension_=20,modelHeading_=0;
    std::string animationId_;
    std::unique_ptr<platform_animation::PlatformAnimation> exteriorAnimation_;
    platform_animation::AnimationFrame exteriorFrame_;
    const platform_animation::CockpitModel* cabinModel_=nullptr;
    std::string modelPath_,modelUpAxis_="z",modelKind_="aircraft";
    bool modelDirty_=true,representative_=false,exteriorReady_=false;
    bool sceneExterior_=false;
    vsg::ref_ptr<vsg::MatrixTransform> body_;
    vsg::ref_ptr<vsg::Switch> root_;
    vsg::ref_ptr<vsg::Group> exteriorGeometry_, photoResources_;
    CockpitPhotoPanel photoPanel_;
    SpatialCockpit spatial_;
    CabinAction hover_=CabinAction::None;
    std::array<std::string,2> pages_;
    unsigned lightStep_=1;
    float cabinDay_=1.f;
    vsg::ref_ptr<vsg::MatrixTransform> stick_,throttle_;
    vsg::ref_ptr<vsg::Switch> interiorDraw_;
    vsg::ref_ptr<vsg::MatrixTransform> interiorBody_, mountedInterior_;
    CockpitInterior interior_;
    std::string cockpitCategory_;
    bool interiorDirty_=true;
};
}
