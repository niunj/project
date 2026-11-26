#pragma once
#include <QWidget>
#include <QElapsedTimer>
#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <nlohmann/json.hpp>
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <map>
class QDoubleSpinBox;class QComboBox;class QCheckBox;class QLabel;class QPushButton;class QVBoxLayout;
namespace vsgraysim::viewport3d {class Viewport3DView;}
namespace vsgraysim::gui::display {
/// Read-only Earth situation display using the same viewport and dataset
/// contract as the scene designer and simulation controller.
class EarthPanel : public QWidget {
public:
    explicit EarthPanel(QWidget* parent=nullptr);
    ~EarthPanel()override;
    bool updateContext(const nlohmann::json&,double simTime,bool online,bool running);
    void updateTelemetry(const nlohmann::json&,uint32_t preferredPlatform=0);
    nlohmann::json contextJson()const{return context_;}
    std::size_t visiblePlatformCount()const{return samples_.size();}
    uint64_t contextRevision()const{return context_revision_;}
    bool surfaceReady(uint64_t revision) const;
    bool renderingAvailable()const{return native_rendering_;}
    viewport3d::Viewport3DView* viewport()const{return viewport_;}
    void setCockpitMode(bool);
    void enableSharedCockpit(){shared_cockpit_=true;}
    void setCockpitTelemetry(const nlohmann::json&,bool fresh);
    void setCockpitView(const nlohmann::json&);
protected:
    void showEvent(QShowEvent*)override;
    void hideEvent(QHideEvent*)override;
private:
    void ensureViewport();
    void applyContext();
    void updateMarkers();
    void focusSelected();
    void updateStatus();
    void updateAcousticMeter();
    void synchronizeCockpit();
    void updatePresentationControl(const nlohmann::json&);
    void sendPresentationPose();
    nlohmann::json selected()const;
    QVBoxLayout* layout_;
    QComboBox* platforms_;
    QComboBox* acoustic_probe_;
    QCheckBox* follow_;
    QCheckBox* audio_;
    QLabel *status_,*placeholder_,*position_,*acoustic_meter_;
    QPushButton* focus_;
    QWidget* poseBar_;
    QDoubleSpinBox *poseYaw_,*posePitch_;
    QPushButton* poseApply_;
    QLabel* poseStatus_;
    uint32_t posePlatform_=0;
    std::string poseRequest_,poseSession_;
    QString poseFeedback_;
    QElapsedTimer poseRequestAge_;
    viewport3d::Viewport3DView* viewport_=nullptr;
    nlohmann::json context_=nlohmann::json::object(),samples_=nlohmann::json::array();
    AsyncVoxelSceneReadView voxel_view_;
    std::string context_key_,applied_context_key_,attempted_context_key_,applied_earth_key_;
    QString error_;
    bool native_rendering_=false,online_=false,running_=false,invalid_context_=false;
    double sim_time_=0;
    uint64_t context_revision_=0;
    vsg::ref_ptr<vsg::Group> markers_;
    vsg::ref_ptr<vsg::Node> marker_shape_;
    std::map<uint32_t,vsg::ref_ptr<vsg::MatrixTransform>> marker_nodes_;
    bool cockpit_mode_=false,shared_cockpit_=false,cockpit_fresh_=false;
    nlohmann::json cockpit_state_=nlohmann::json::object(),cockpit_view_={{"view_mode","pilot"}};
};
}
