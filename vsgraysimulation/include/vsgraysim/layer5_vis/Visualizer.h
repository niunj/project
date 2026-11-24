#pragma once
/// @file Visualizer.h
/// @brief Layer 5: VSG-based visualization & data output (section 1.1, layer 5).
///
/// Owns the VSG viewer, the camera-manipulator event handlers, and the
/// per-frame update hook that:
///   * overlays atmospheric attenuation/scatter on the raster view;
///   * draws cross-induction paths as coloured line segments connecting
///     each tx sensor's position to the rx aperture that detected the
///     crosstalk event;
///   * keeps the offscreen per-band sensor image textures up to date.

#include <memory>
#include <functional>
#include <vector>
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>
#include <vsg/app/Viewer.h>
#include <vsg/app/Window.h>
#include <vsg/app/Camera.h>
#include <vsg/vk/Device.h>

#include "vsgraysim/layer0_phys/AtmosphereModel.h"
#include "vsgraysim/layer0_phys/CrossInductionRules.h"

namespace vsgraysim {

struct CrossInductionPath {
    vsg::dvec3 tx_pos;
    vsg::dvec3 rx_pos;
    uint32_t   tx_sensor_id;
    uint32_t   rx_sensor_id;
    float      power;
};

class Visualizer {
public:
    Visualizer();
    ~Visualizer();

    /// Initialise the VSG viewer with a single window of the given size.
    bool init(int width, int height);

    /// Set the scene root that will be rendered.
    void setScene(vsg::ref_ptr<vsg::Node> root);

    /// Replace the list of cross-induction paths to draw this frame.
    void setCrossInductionPaths(const std::vector<CrossInductionPath>& paths);

    /// Update the atmospheric overlay (e.g. fog density, aerosol color)
    /// driven by the current AtmosphereModel.
    void updateAtmosphereOverlay(const AtmosphereModel& atm);

    /// Per-band sensor image update (RGBA float, width*height*4).
    void updateSensorImage(uint32_t band_channel,
                           uint32_t width, uint32_t height,
                           const std::vector<float>& image);

    /// Run the viewer's frame loop once; returns false when the viewer
    /// should close.
    bool frame();

    /// Close the viewer.
    void close();

    /// Register a per-frame ImGui draw callback (called each VSG frame by
    /// the vsgImGui overlay).  Returns false when the overlay is not
    /// available (e.g. headless / no ImGui support).
    bool addImGui(std::function<void()> draw);

    /// True when the in-viewport ImGui overlay is active.
    bool imguiAvailable() const { return imgui_ != nullptr; }

    /// Access the VSG device (nullptr in headless mode).
    vsg::Device* device() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    vsg::ref_ptr<vsg::Node> imgui_;   ///< vsgImGui::RenderImGui node
};

using VisualizerPtr = std::shared_ptr<Visualizer>;

} // namespace vsgraysim
