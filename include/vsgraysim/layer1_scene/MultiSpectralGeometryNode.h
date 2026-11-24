#pragma once
/// @file MultiSpectralGeometryNode.h
/// @brief Layer 1: VSG geometry node carrying per-face multi-spectral
///        physical-parameter indices (section 2.2.3).
///
/// Extends vsg::Group so it can carry both a renderable child graph and
/// sideband data (geometry-unit parameter indices) used by the RT pipeline.
/// The physical parameters themselves live in the GPU SSBO maintained by
/// Layer 0 (PhysParamManager) - this node only stores the index mapping
/// that ties each face range to its `GeoUnitPhysParams` entry.
///
/// VSG visitors do not have an `apply(MultiSpectralGeometryNode&)` overload,
/// so we explicitly forward `accept()` to the `Group` overload. This keeps
/// the scene-graph traversal working unchanged while letting the RT layer
/// query the bindings through normal C++ accessors.

#include <vsg/core/Inherit.h>
#include <vsg/core/Visitor.h>
#include <vsg/app/RecordTraversal.h>
#include <vsg/nodes/Group.h>

#include <cstdint>
#include <string>
#include <vector>
#include "vsgraysim/core/BandTypes.h"

namespace vsgraysim {

/// One face-range -> geo_unit_id mapping entry held by the node.
struct FaceRangeBinding {
    uint32_t    face_start;
    uint32_t    face_end;
    uint32_t    geo_unit_id_base; ///< `geo_unit_id` of `face_start`
    uint32_t    object_id;
};

class MultiSpectralGeometryNode : public vsg::Inherit<vsg::Group, MultiSpectralGeometryNode> {
public:
    MultiSpectralGeometryNode() = default;
    explicit MultiSpectralGeometryNode(std::string object_id);

    /// Bind a face range to a contiguous range of geometry-unit IDs.
    void bindFaceRange(uint32_t face_start, uint32_t face_end,
                       uint32_t geo_unit_id_base, uint32_t object_id);

    /// Lookup the `geo_unit_id` for a given face id.
    /// Returns `UINT32_MAX` if the face is not bound.
    uint32_t lookupGeoUnit(uint32_t face_id) const;

    const std::string& objectId() const { return object_id_; }
    void setObjectId(std::string id) { object_id_ = std::move(id); }
    const std::vector<FaceRangeBinding>& bindings() const { return bindings_; }

    /// Mask of wavebands currently bound on this node.
    void setBandMask(uint32_t m) { band_mask_ = m; }
    uint32_t bandMask() const { return band_mask_; }

    // Visitor dispatch: forward to vsg::Group so that the standard
    // RecordTraversal pipeline renders this node's children normally.
    void accept(vsg::Visitor& v) override { v.apply(static_cast<vsg::Group&>(*this)); }
    void accept(vsg::ConstVisitor& v) const override { v.apply(static_cast<const vsg::Group&>(*this)); }
    void accept(vsg::RecordTraversal& v) const override { v.apply(static_cast<const vsg::Group&>(*this)); }

private:
    std::string                    object_id_;
    std::vector<FaceRangeBinding>  bindings_;
    uint32_t                       band_mask_ = BAND_VIS | BAND_IR | BAND_RADAR | BAND_SAR;
};

} // namespace vsgraysim
