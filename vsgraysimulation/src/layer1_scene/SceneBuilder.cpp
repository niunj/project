#include "vsgraysim/layer1_scene/SceneBuilder.h"
#include "vsgraysim/core/ObjLoader.h"

#include <spdlog/spdlog.h>
#include <vsg/io/read.h>
#include <vsg/io/Options.h>
#include <vsgXchange/all.h>

#include <filesystem>
#include <algorithm>
#include <cctype>

namespace vsgraysim {

SceneBuilder::SceneBuilder(PhysParamManagerPtr phys,
                           PhysicsEnginePtr physics,
                           vsg::ref_ptr<vsg::Options> options)
    : phys_(std::move(phys)), physics_(std::move(physics)), options_(std::move(options)) {
    if (!options_) {
        options_ = vsg::Options::create();
        // Comprehensive search paths so the GUI (which may run from
        // build/, build/gui/, or the project root) can always find the
        // models and config directories.
        options_->paths = {
            "models", "config",
            "../models", "../config",
            "../../models", "../../config",
            "../build/models", "build/models",
            "../../build/models",
        };
        // Register the vsgXchange readers (gltf, assimp, gdal, ...)
        if (auto ax = vsgXchange::all::create()) {
            options_->readerWriters = { ax };
        }
    }
}

vsg::ref_ptr<vsg::Group> SceneBuilder::build() {
    // Idempotent: build() may be called once per pipeline consumer
    // (ray tracer, visualizer, ...).  Re-running it would reload every
    // mesh and try to re-create duplicate rigid bodies.
    if (root_) return root_;

    root_ = vsg::Group::create();
    ms_nodes_.clear();
    dyn_nodes_.clear();

    // Helper: try to resolve a relative mesh path by checking the
    // options_->paths search list and a few common build directories.
    auto resolveMeshPath = [&](const std::string& mesh_path) -> std::string {
        namespace fs = std::filesystem;
        // 1. Absolute or explicitly relative path that exists as-is.
        if (fs::exists(mesh_path)) return mesh_path;
        // 2. Try each option search path.
        for (const auto& p : options_->paths) {
            std::string sp = p.string();
            fs::path candidate = fs::path(sp) / mesh_path;
            if (fs::exists(candidate)) return candidate.string();
            // Also try with the path stripped of "models/" prefix since
            // the option path may already point at the models dir.
            if (mesh_path.find("models/") == 0) {
                fs::path c2 = fs::path(sp) / mesh_path.substr(7);
                if (fs::exists(c2)) return c2.string();
            }
        }
        // 3. Try a few hardcoded build-relative locations.
        static const char* build_roots[] = {
            "build/models", "../build/models", "../../build/models",
            "models", "../models", "../../models",
        };
        for (const auto* r : build_roots) {
            fs::path candidate = fs::path(r) / mesh_path;
            if (fs::exists(candidate)) return candidate.string();
            if (mesh_path.find("models/") == 0) {
                fs::path c2 = fs::path(r) / mesh_path.substr(7);
                if (fs::exists(c2)) return c2.string();
            }
        }
        return mesh_path;  // return original; vsg::read will fail gracefully
    };

    for (const auto& cfg : phys_->objectConfigs()) {
        auto node = MultiSpectralGeometryNode::create(cfg.object_id);

        // Try to load the model mesh; if it fails we still create the node
        // with empty children so the binding table is preserved.  Detailed
        // diagnostics are logged by loadObjNode / vsg::read on failure.
        vsg::ref_ptr<vsg::Node> mesh;
        if (!cfg.mesh_path.empty()) {
            std::string resolved = resolveMeshPath(cfg.mesh_path);
            if (resolved != cfg.mesh_path) {
                spdlog::info("Layer1: resolved mesh path '{}' -> '{}'",
                             cfg.mesh_path, resolved);
            } else {
                // The path was returned unchanged - check whether the file
                // actually exists so we can warn early with a useful msg.
                namespace fs = std::filesystem;
                if (!fs::exists(resolved)) {
                    spdlog::error("Layer1: object '{}' - mesh file does "
                                  "not exist: '{}' (searched options_->paths "
                                  "and build_roots; see preceding logs)",
                                  cfg.object_id, resolved);
                }
            }

            // Try vsg::read first (handles .gltf/.glb/.osg).  Fall back to
            // our built-in .obj parser for .obj files - vsgXchange's
            // assimp reader is a stub in this environment.
            mesh = vsg::read_cast<vsg::Node>(resolved, options_);
            if (!mesh) {
                // Check extension to decide whether the .obj fallback applies.
                auto dot = resolved.find_last_of('.');
                if (dot != std::string::npos) {
                    std::string ext = resolved.substr(dot);
                    // Lowercase comparison.
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c){ return std::tolower(c); });
                    if (ext == ".obj") {
                        spdlog::info("Layer1: vsg::read failed for '{}', "
                                     "using built-in .obj parser "
                                     "(expected - assimp is a stub)",
                                     resolved);
                        mesh = loadObjNode(resolved, options_);
                    } else if (ext == ".gltf" || ext == ".glb" ||
                               ext == ".osg" || ext == ".osgb" ||
                               ext == ".vsgb") {
                        spdlog::error("Layer1: vsg::read returned null "
                                      "for '{}' (ext='{}') - the file may "
                                      "be corrupt or reference missing "
                                      "external resources",
                                      resolved, ext);
                    } else if (ext == ".fbx" || ext == ".dae" ||
                               ext == ".3ds" || ext == ".ply" ||
                               ext == ".stl") {
                        spdlog::error("Layer1: vsg::read returned null "
                                      "for '{}' - extension '{}' requires "
                                      "assimp which is a stub in "
                                      "vsgXchange. Convert to .gltf/.glb "
                                      "or .obj to load.",
                                      resolved, ext);
                    } else {
                        spdlog::error("Layer1: vsg::read returned null "
                                      "for '{}' (unsupported extension "
                                      "'{}'). Supported: .gltf/.glb/.osg/"
                                      ".osgb/.vsgb (vsg::read), .obj "
                                      "(built-in parser).",
                                      resolved, ext);
                    }
                } else {
                    spdlog::error("Layer1: vsg::read returned null for "
                                  "'{}' and file has no extension - "
                                  "cannot pick a fallback loader",
                                  resolved);
                }
            }
        } else {
            spdlog::warn("Layer1: object '{}' has empty mesh_path - "
                         "creating node without renderable mesh",
                         cfg.object_id);
        }
        if (mesh) {
            if (cfg.scale!=1.0) {
                auto scaled=vsg::MatrixTransform::create(vsg::scale(cfg.scale,cfg.scale,cfg.scale));
                scaled->addChild(mesh); mesh=scaled;
            }
            // Wrap the mesh in a MatrixTransform when the object has a
            // physics configuration.  Bullet owns the matrix (MotionState)
            // and updates it every physics tick; the rest of the pipeline
            // (RT, sensors, visualizer) reads the same node.
            if (physics_) {
                RigidBodyDef def;
                def.object_id            = cfg.object_id;
                def.start_position       = cfg.initial_position;
                def.start_rotation_deg   = cfg.initial_rotation_deg;
                def.initial_velocity     = cfg.initial_velocity;
                def.mass                 = cfg.mass;
                def.restitution          = cfg.restitution;
                def.friction             = cfg.friction;
                def.use_convex_hull_shape = cfg.use_convex_hull;
                if (auto mt = physics_->createRigidBodyFromMesh(mesh, def)) {
                    node->addChild(mt);
                    physics_transforms_[cfg.object_id] = mt;
                    spdlog::info("Layer1: object '{}' - mesh wrapped in physics "
                                 "rigid-body transform", cfg.object_id);
                } else {
                    node->addChild(mesh);
                }
            } else {
                node->addChild(mesh);
            }
            spdlog::info("Layer1: loaded mesh '{}' for object '{}'",
                         cfg.mesh_path, cfg.object_id);
        } else if (!cfg.mesh_path.empty()) {
            spdlog::warn("Layer1: object '{}' - all loaders failed for "
                         "mesh '{}'; creating node without renderable "
                         "mesh (ray tracing will use the empty bbox)",
                         cfg.object_id, cfg.mesh_path);
        }

        // Bind face ranges from the Layer-0 config.
        for (const auto& g : cfg.face_groups) {
            const auto* material=phys_->findFace(cfg.object_id,g.face_start);
            if (!material) throw std::runtime_error("missing scene face material");
            const uint32_t object_id_hash=material->object_id;
            node->bindFaceRange(g.face_start, g.face_end,
                                material->geo_unit_id,
                                object_id_hash);
        }

        // Mark dynamic objects (engine temp updates, etc.) - if the
        // config asks for temperature updates, wrap in DynamicMultiSpectralNode.
        if (cfg.temp_update_rate_hz > 0) {
            auto dyn = DynamicMultiSpectralNode::create();
            dyn->setObjectId(cfg.object_id);
            // Move children + bindings into the dynamic variant.
            for (auto& c : node->children) dyn->addChild(c);
            for (auto& b : node->bindings()) {
                dyn->bindFaceRange(b.face_start, b.face_end,
                                   b.geo_unit_id_base, b.object_id);
            }
            dyn->setBandMask(node->bandMask());
            root_->addChild(dyn);
            dyn_nodes_.push_back(dyn);
            spdlog::debug("Layer1: object '{}' is dynamic (temp @ {}Hz)",
                          cfg.object_id, cfg.temp_update_rate_hz);
        } else {
            root_->addChild(node);
            ms_nodes_.push_back(node);
        }
    }

    // Add sensor aperture debug geometry placeholders - real apertures are
    // attached via addSensorAperture() at the application layer.
    for (const auto& a : apertures_) {
        if (a.debug_geom) root_->addChild(a.debug_geom);
    }

    spdlog::info("Layer1: scene built ({} ms nodes, {} dynamic nodes, {} apertures)",
                 ms_nodes_.size(), dyn_nodes_.size(), apertures_.size());
    return root_;
}

void SceneBuilder::addSensorAperture(const SensorAperture& a) {
    apertures_.push_back(a);
}

} // namespace vsgraysim
