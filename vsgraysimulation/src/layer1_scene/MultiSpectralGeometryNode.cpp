#include "vsgraysim/layer1_scene/MultiSpectralGeometryNode.h"

#include <algorithm>
#include <limits>

namespace vsgraysim {

MultiSpectralGeometryNode::MultiSpectralGeometryNode(std::string object_id)
    : object_id_(std::move(object_id)) {}

void MultiSpectralGeometryNode::bindFaceRange(uint32_t face_start,
                                             uint32_t face_end,
                                             uint32_t geo_unit_id_base,
                                             uint32_t object_id) {
    bindings_.push_back({face_start, face_end, geo_unit_id_base, object_id});
    // Keep bindings sorted by face_start for fast lookup.
    std::sort(bindings_.begin(), bindings_.end(),
              [](const FaceRangeBinding& a, const FaceRangeBinding& b) {
                  return a.face_start < b.face_start;
              });
}

uint32_t MultiSpectralGeometryNode::lookupGeoUnit(uint32_t face_id) const {
    // Linear scan is fine for small binding counts; binary search for large.
    for (const auto& b : bindings_) {
        if (face_id >= b.face_start && face_id <= b.face_end) {
            return b.geo_unit_id_base + (face_id - b.face_start);
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

} // namespace vsgraysim
