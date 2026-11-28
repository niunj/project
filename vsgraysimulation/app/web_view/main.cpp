#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/earth/EarthDataService.h>
#include <vsgraysim/earth/OrbitalGlobe.h>
#include <VoxelDeploymentAdapter.h>
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/AircraftExterior.h"
#include <simcore/Orientation.h>
#include "viewport3d/PlatformMotionFeature.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/VoxelSceneFeature.h"
#include "viewport3d/VsgQtWidget.h"
#include <simcore/GeoNavigation.h>

#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QTimer>
#include <vsg/all.h>
#include <algorithm>
#include <cmath>
#include <future>
#include <iostream>
#include <unistd.h>

namespace {
using Json = nlohmann::json;
using Vec = vsg::dvec3;
using namespace vsgraysim;
using namespace vsgraysim::viewport3d;

Json read(const QString& file) {
    QFile source(file);
    if (!source.open(QIODevice::ReadOnly) || source.size() > 1024 * 1024) return nullptr;
    const auto data = source.readAll();
    return Json::parse(data.begin(), data.end(), nullptr, false);
}

Vec vector(const Json& a) {
    return {a.at(0).get<double>(), a.at(1).get<double>(), a.at(2).get<double>()};
}

void positionCamera(Viewport3DView& view, simcore::geo::GeoAnchor anchor,
                    const Json& control, const Json& sceneCamera) {
    if (!view.camera()) return;
    auto look = view.camera()->viewMatrix.cast<vsg::LookAt>();
    if (!look) return;
    const auto mode = control.at("preset").get<std::string>();
    const simcore::geo::EarthNavigation input{control.at("yaw_deg").get<double>(), control.at("pitch_deg").get<double>(),
        control.at("zoom").get<double>(), control.value("pan_x",0.), control.value("pan_y",0.)};
    Vec aim{0.,500.,0.}, offset{8400.,7900.,-10300.};
    if (mode != "globe" && sceneCamera.is_object()) {
        aim = vector(sceneCamera.at("target_m"));
        offset = vector(sceneCamera.at("eye_m")) - aim;
    }
    if (control.contains("focus_m")) aim = vector(control.at("focus_m"));
    auto pose = simcore::geo::workspaceEarthCamera(anchor, mode == "globe", input, aim, offset);
    if (mode == "workspace") {
        auto orbital = input;
        orbital.zoom = (simcore::geo::GeoAnchor::kEarthA + vsg::length(offset) * input.zoom) / 17.8e6;
        const double transition = std::clamp(std::log(input.zoom / 20.) / std::log(12.5), 0., 1.);
        const double blend = transition * transition * (3. - 2. * transition);
        const auto globe = simcore::geo::workspaceEarthCamera(anchor, true, orbital);
        pose.eye = pose.eye * (1. - blend) + globe.eye * blend;
        pose.center = pose.center * (1. - blend) + globe.center * blend;
        pose.up = vsg::normalize(pose.up * (1. - blend) + globe.up * blend);
    }
    look->eye = pose.eye;
    look->center = pose.center;
    look->up = pose.up;
    if (auto perspective = view.camera()->projectionMatrix.cast<vsg::Perspective>()) {
        const auto clip = simcore::geo::cameraClip(pose);
        perspective->nearDistance = clip.first;
        perspective->farDistance = clip.second;
    }
}

class Capture {
public:
    explicit Capture(QString directory) : directory_(std::move(directory)) {
        view_.setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
        view_.setAttribute(Qt::WA_ShowWithoutActivating);
        view_.resize(1440, 900);
        view_.move(-10000, -10000);
    }

    void update() {
        const auto request = read(directory_ + "/request.json");
        if (!request.is_object()) return;
        const bool globe = request.at("camera").at("preset") == "globe";
        if (viewId_.empty()) load(request, globe);
        else if (request.at("epoch") != epoch_) { QCoreApplication::quit(); return; }
        frameIntervalMs_ = request.value("frame_interval_ms",33);
        if (!view_.isReady()) return;
        if (request.at("model").is_object()) {
            if (!studio_) mountModel(request.at("model"));
            const auto revision = request.at("camera_revision").get<int64_t>();
            if (revision != cameraRevision_) {
                positionModel(request.at("camera"));
                cameraRevision_ = revision;
                cameraInputRevision_ = request.at("camera").value("input_revision",int64_t(-1));
                pendingFrames_ = 2;
            }
            if (pendingFrames_ > 0) capture(0);
            return;
        }
        auto* voxel = view_.featureAs<VoxelSceneFeature>("voxel_scene");
        auto* earth = view_.featureAs<EarthViewFeature>(EarthView::kId);
        if (!earth || (!voxel && !orbital_)) return;
        auto wasChanged = pendingFrames_ > 0;
        if (remote_ && voxel) {
            const auto status = read(statusPath_);
            if (status.contains("earth") && status.at("earth").contains("voxel_scene"))
                remote_->submit(status.at("earth").at("voxel_scene"));
            Json presentation, visuals;
            if (auto state = remote_->take(&presentation, &visuals)) {
                voxel->bindSnapshot(state, std::move(presentation), std::move(visuals));
                wasChanged = true;
            }
        } else if (voxel && voxel->runtime() && sharedEarth_) {
            if (applySharedLayout(*voxel->runtime(),request)) {
                wasChanged = true;
                pendingFrames_ = 4;
            }
        } else if (voxel && !layoutApplied_ && voxel->runtime()) {
            const auto deployment = request.value("deployment",Json(nullptr));
            if (deployment.is_object()) {
                earth::deployment::Adapter adapter(deployment.at("catalog").get<std::string>());
                adapter.applyInWorker(*voxel->runtime(),projectDocument_,deployment.at("plan"),
                    request.at("topic").get<std::string>());
            }
            applyVoxelInitialPositions(*voxel->runtime(), request.at("positions"));
            applyVoxelModelMaterials(*voxel->runtime(), request.value("model_bindings",Json::object()));
            layoutApplied_ = true;
            wasChanged = true;
        }
        const auto scene = voxel ? voxel->snapshot() : nullptr;
        const auto ground = earth->surfaceSnapshot();
        if ((orbital_ && !ground) || (!orbital_ && (!scene || (!remote_ && !layoutApplied_)))) {
            view_.renderFrame(); // Pump the existing asynchronous terrain and model loaders.
            return;
        }
        if (sharedEarth_ && !updateBackdrop(*earth,*voxel,scene->anchor,request.at("camera"))) {
            view_.renderFrame();
            return;
        }
        const auto groundRevision = ground ? ground->revision : 0;
        auto revision = request.at("camera_revision").get<int64_t>();
        if (revision != cameraRevision_ || groundRevision != groundRevision_) {
            positionCamera(view_, scene ? scene->anchor : ground->anchor, request.at("camera"), (remote_ || request.at("camera").at("preset")=="scene") ? camera_ : Json(nullptr));
            cameraRevision_ = revision;
            cameraInputRevision_ = request.at("camera").value("input_revision",int64_t(-1));
            cameraZoom_ = request.at("camera").at("zoom").get<double>();
            groundRevision_ = groundRevision;
            pendingFrames_ = 4;
            wasChanged = true;
        }
        if (orbital_) {
            earth->setCloudPresentationEnabled(false);
            if (auto* ocean = earth->environmentRoot(EarthViewFeature::EnvironmentLayer::GlobalSea))
                ocean->children.clear();
        }
        view_.context().setSimulationTime(scene ? scene->timeSeconds() : 0.);
        if (wasChanged) {
            pendingFrames_ = std::max(1,pendingFrames_);
            capture(scene ? scene->tick : 0);
        } else if (view_.widget()->property("earthLoading").toBool()) view_.renderFrame();
    }

private:
    void capture(uint64_t tick) {
        if (frameIntervalMs_ > 33 && captureClock_.isValid() && captureClock_.elapsed() < frameIntervalMs_) return;
        captureClock_.start();
        QElapsedTimer time;
        time.start();
        const auto image = view_.renderAndGrabFrame();
        const double ms = time.nsecsElapsed() / 1e6;
        if (image.isNull()) return;

        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        time.restart();
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPEG", 86)) return;
        Json stamp = {{"view_id",viewId_},{"epoch",epoch_},{"sequence",std::to_string(++sequence_)},
                      {"tick",std::to_string(tick)},{"render_ms",ms},{"encode_ms",time.nsecsElapsed()/1e6}};
        if (cameraInputRevision_ >= 0) stamp["camera_input_revision"] = cameraInputRevision_;
        if (groundRevision_) stamp["ground_revision"] = groundRevision_;
        if (sharedEarth_) stamp["layout_revision"] = layoutRevision_;
        if (sharedEarth_) stamp["camera_zoom"] = cameraZoom_;
        QSaveFile output(directory_ + "/latest.frame");
        const auto header = QByteArray::fromStdString(stamp.dump() + "\n");
        if (output.open(QIODevice::WriteOnly) && output.write(header) == header.size() &&
            output.write(jpeg) == jpeg.size() && output.commit() && pendingFrames_ > 0) --pendingFrames_;
    }

    bool updateBackdrop(EarthViewFeature& map,VoxelSceneFeature& voxel,const simcore::geo::GeoAnchor& anchor,
                        const Json& camera) {
        const bool orbitalDistance = camera.at("zoom").get<double>() >= 52.;
        if (!orbitalGlobe_ && !orbitalPreparation_.valid())
            orbitalPreparation_ = std::async(std::launch::async,[anchor] {
                return earth::EarthDataService::loadMap("config/earth/global-voxel.map.json",anchor,{{"orbital_preview",true}});
            });
        if (!orbitalGlobe_ && orbitalPreparation_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            const auto backdrop = orbitalPreparation_.get();
            if (!backdrop->orbital) throw std::runtime_error("global voxel Earth preview is unavailable");
            orbitalGlobe_ = std::make_unique<earth::OrbitalGlobe>(backdrop->orbital,map.cloudResources());
            orbitalLayer_ = vsg::Switch::create();
            orbitalLayer_->addChild(orbitalDistance,orbitalGlobe_->node);
            view_.context().worldRoot()->addChild(orbitalLayer_);
        }
        if (orbitalDistance && !orbitalGlobe_) return false;
        if (orbitalDistance != showingOrbital_) {
            showingOrbital_ = orbitalDistance;
            orbitalLayer_->setAllChildren(showingOrbital_);
            voxel.setEnabled(!showingOrbital_);
            view_.context().requestCompile();
        }
        if (orbitalGlobe_ && orbitalDistance) if (const auto* weather = map.weather()) {
            orbitalGlobe_->setSun(weather->sun);
            orbitalGlobe_->setLighting(weather->moon,weather->moonPhase,weather->exposure,weather->turbidity);
            orbitalGlobe_->setWeather(*weather);
        }
        return true;
    }

    bool applySharedLayout(earth::VoxelScene& runtime,const Json& request) {
        const auto revision = request.at("layout_revision").get<int64_t>();
        if (layoutApplied_ && revision == layoutRevision_) return false;
        if (!authored_) {
            auto terrain = Json::array();
            for (const auto& [id,object] : runtime.snapshot()->objects)
                if (object.earth && object.terrainMode != "voxel")
                    terrain.push_back({{"kind","terrain_mode"},{"object",id},{"mode","voxel"}});
            if (!terrain.empty()) runtime.edit(terrain,runtime.snapshot()->revision);
            authored_ = runtime.snapshot();
        }
        if (overlaysApplied_) runtime.restoreAuthored(authored_,runtime.snapshot()->revision);
        const auto deployment = request.value("deployment",Json(nullptr));
        if (deployment.is_object()) {
            auto plan = deployment.at("plan");
            auto& actions = plan.at("actions");
            const auto current = runtime.snapshot();
            actions.erase(std::remove_if(actions.begin(),actions.end(),[&](const auto& action) {
                return current->objects.count(action.at("id").template get<std::string>()) != 0;
            }),actions.end());
            if (!actions.empty()) {
                earth::deployment::Adapter adapter(deployment.at("catalog").get<std::string>());
                adapter.applyInWorker(runtime,projectDocument_,plan,request.at("topic").get<std::string>());
                authored_ = runtime.snapshot();
            }
        }
        const auto& positions = request.at("positions");
        const auto& materials = request.at("model_bindings");
        applyVoxelInitialPositions(runtime,positions);
        applyVoxelModelMaterials(runtime,materials);
        overlaysApplied_ = !positions.empty() || !materials.empty();
        layoutApplied_ = true;
        layoutRevision_ = revision;
        return true;
    }

    void mountModel(const Json& specification) {
        vsg::ref_ptr<vsg::Node> model;
        if (specification.at("representation") == "original") {
            bool substitute = false;
            auto options = vsg::Options::create(*view_.options());
            options->sceneCoordinateConvention = vsg::CoordinateConvention::Y_UP;
            model = loadAircraftExterior(specification.at("path").get<std::string>(),
                specification.at("up_axis").get<std::string>(), {}, 8., options, substitute, 0.);
            if (substitute) throw std::runtime_error("模型原件无法读取");
        } else {
            auto preparation = earth::VoxelScene::prepareImportAsync(specification.at("definition"));
            const auto snapshot = preparation.snapshot.get();
            const auto display = earth::makeVoxelSceneDisplay(*snapshot);
            model = display.node;
        }
        if (!model) throw std::runtime_error("模型没有可渲染表面");
        vsg::ComputeBounds measured;
        model->accept(measured);
        if (!measured.bounds.valid()) throw std::runtime_error("模型边界无效");
        const auto center = (measured.bounds.max + measured.bounds.min) * .5;
        const auto range = measured.bounds.max - measured.bounds.min;
        const auto span = std::max({range.x, range.y, range.z});
        if (!std::isfinite(span) || span < 1.e-8) throw std::runtime_error("模型尺寸无效");
        const auto angle = vector(specification.at("rotation_deg"));
        studio_ = vsg::MatrixTransform::create();
        studio_->matrix = vsg::rotate(simcore::headingRotation(angle.y,angle.x,angle.z)) *
            vsg::scale(8./span,8./span,8./span) * vsg::translate(-center);
        studio_->addChild(model);
        view_.context().worldRoot()->addChild(studio_);

        vsg::ComputeBounds framed;
        studio_->accept(framed);
        auto builder = vsg::Builder::create();
        builder->options = view_.options();
        vsg::GeometryInfo stand;
        stand.position = {0, float(framed.bounds.min.y - .2), 0};
        stand.dx = {10.4f,0,0}; stand.dy = {0,0,-10.4f}; stand.dz = {0,.24f,0};
        stand.color = vsg::vec4(.045f,.075f,.09f,1.f);
        vsg::StateInfo finish;
        finish.lighting = true;
        finish.two_sided = true;
        view_.context().worldRoot()->addChild(builder->createCylinder(stand, finish));
        for (const auto& task : view_.viewer()->recordAndSubmitTasks)
            for (const auto& graph : task->commandGraphs)
                for (const auto& child : graph->children)
                    if (auto render = child.cast<vsg::RenderGraph>())
                        render->setClearValues({{.025f,.038f,.052f,1.f}});
        view_.context().requestCompile();
    }

    void positionModel(const Json& camera) {
        auto* handle = view_.camera();
        auto look = handle ? handle->viewMatrix.cast<vsg::LookAt>() : nullptr;
        if (!look) return;
        const double yaw = vsg::radians(31. + camera.at("yaw_deg").get<double>());
        const double altitude = vsg::radians(std::clamp(23. + camera.at("pitch_deg").get<double>(), -22., 78.));
        const double radius = std::clamp(14. * camera.at("zoom").get<double>(),6.,32.);
        look->eye = {radius*std::cos(altitude)*std::sin(yaw),radius*std::sin(altitude),
                     radius*std::cos(altitude)*std::cos(yaw)};
        look->center = {0.,-.12,0.};
        look->up = {0.,1.,0.};
        if (auto p = handle->projectionMatrix.cast<vsg::Perspective>()) {
            p->fieldOfViewY=36.;
            p->nearDistance=.08;
            p->farDistance=150.;
        }
    }

    void load(const Json& request, bool globe) {
        viewId_ = request.at("id").get<std::string>();
        epoch_ = request.at("epoch").get<std::string>();
        if (request.at("model").is_object()) {
            auto config = ViewportConfig::fromPreset("model_studio");
            config.remove("camera_guard");
            view_.applyConfig(config);
            view_.show();
            return;
        }
        const auto topic = request.at("topic").get<std::string>();
        auto config = ViewportConfig::fromTopicFile(topic, "earth_display");
        const auto project = VoxelSceneProject::open(topic);
        sharedEarth_ = request.value("shared_earth",false);
        projectDocument_ = project.document;
        camera_ = project.document.value("camera", Json(nullptr));
        statusPath_ = QString::fromStdString(request.at("status_path").get<std::string>());
        remote_.reset();
        if (globe) {
            config.useWorkspaceGlobe();
        } else if (!statusPath_.isEmpty()) {
            remote_ = std::make_unique<AsyncVoxelSceneReadView>();
            config.remove("voxel_scene");
            config.add("voxel_scene", {{"external_only",true},
                                      {"terrain_display_paging",project.packageManifest.contains("terrain_pages")}});
            config.add(ScenePlatformMotion::kId);
        } else {
            const auto root = project.document.contains("checkpoint") ? project.document.at("checkpoint") : Json(nullptr);
            Json params = {{"scene_json",project.scenePath},{"paused",true},{"background_loading",true},{"frame_scene",false}};
            if (root.is_object()) params["checkpoint"] = root;
            config.add("voxel_scene", params);
        }
        Json world = Json::object();
        for (const auto& entry : config.entries())
            if (entry.id == EarthView::kId) world = entry.params;
        world.update({{"scene_context",true},{"background_loading",true}});
        config.add(EarthView::kId, world);
        config.remove("scene_audio");
        orbital_ = globe;
        layoutApplied_ = false;
        cameraRevision_ = -1;
        groundRevision_ = 0;
        pendingFrames_ = 4;
        view_.applyConfig(config);
        if (!view_.isVisible()) view_.show();
    }

    QString directory_, statusPath_;
    Viewport3DView view_;
    vsg::ref_ptr<vsg::MatrixTransform> studio_;
    std::unique_ptr<AsyncVoxelSceneReadView> remote_;
    std::string viewId_, epoch_;
    Json camera_, projectDocument_;
    earth::VoxelSceneSnapshotPtr authored_;
    std::future<earth::EarthSurfaceSnapshotPtr> orbitalPreparation_;
    std::unique_ptr<earth::OrbitalGlobe> orbitalGlobe_;
    vsg::ref_ptr<vsg::Switch> orbitalLayer_;
    uint64_t sequence_ = 0, groundRevision_ = 0;
    int64_t cameraRevision_ = -1;
    int64_t cameraInputRevision_ = -1;
    int64_t layoutRevision_ = -1;
    double cameraZoom_ = 1.;
    QElapsedTimer captureClock_;
    int frameIntervalMs_ = 33;
    int pendingFrames_ = 0;
    bool layoutApplied_ = false;
    bool orbital_ = false;
    bool sharedEarth_ = false, overlaysApplied_ = false;
    bool showingOrbital_ = false;
};
}

int main(int argc, char** argv) {
#ifdef QT6_PLUGIN_PATH
    qputenv("QT_PLUGIN_PATH", QT6_PLUGIN_PATH);
#endif
    qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);
    if (argc != 2) return 64;
    Capture capture(QString::fromUtf8(argv[1]));
    QSocketNotifier parent(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&parent, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        try {
            capture.update();
        } catch (const std::exception& error) {
            std::cerr << "web viewport: " << error.what() << '\n';
            app.exit(2);
        }
    });
    timer.setTimerType(Qt::PreciseTimer);
    timer.start(33);
    return app.exec();
}
