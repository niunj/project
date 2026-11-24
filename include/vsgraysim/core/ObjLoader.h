#pragma once
/// @file ObjLoader.h
/// @brief Minimal Wavefront .obj loader that produces a vsg::Node.
///
/// vsgXchange ships with an `assimp` reader that would normally handle
/// .obj/.fbx/.dae/etc.  However, the vsgXchange library installed in
/// this environment was compiled **without** assimp support, so the
/// `assimp` reader is a stub whose `read()` always returns `nullptr`
/// (see `assimp_fallback.cpp.o` inside `libvsgXchange.a`).  As a
/// consequence `vsg::read_cast<vsg::Node>("foo.obj")` silently fails
/// for every .obj file.
///
/// This loader is a self-contained fallback that supports the subset
/// of Wavefront .obj actually used by the project's models:
///   * `v`            vertex positions
///   * `vn`           vertex normals (used directly when present)
///   * `vt`           texture coordinates (accepted but not textured -
///                    a flat per-material diffuse colour is used instead)
///   * `f v/vt/vn`     faces (fan-triangulated; missing vt/vn tolerated)
///   * `mtllib`       material library (.mtl) reference
///   * `usemtl`       active material assignment
///   * `o`/`g`        object/group names (ignored)
///
/// The .mtl parser understands `newmtl`, `Kd` (diffuse colour) and
/// `map_Kd` (diffuse texture - accepted but not loaded as a texture;
/// the Kd colour is used as a flat material instead).  Each material
/// becomes its own vsg::StateGroup + vsg::VertexIndexDraw so faces
/// with different materials render with their correct colours.
///
/// The resulting node is wrapped in a Group containing one StateGroup
/// per material, each using vsg::Builder's Phong shader with the
/// material's diffuse colour.  When the .obj has no .mtl or the
/// referenced material is missing, a default mid-grey material is
/// used so the geometry is always visible.

#include <string>
#include <vector>

#include <vsg/core/ref_ptr.h>
#include <vsg/io/Options.h>
#include <vsg/maths/vec4.h>

namespace vsg { class Node; class Options; }

namespace vsgraysim {

/// Per-material-slot info (one slot per output StateGroup/VertexIndexDraw,
/// in root-child order).  Consumed by the sensor chain to bind diffuse
/// textures (map_Kd) and Kd colours per material bucket.
struct ObjMaterialSlot
{
    std::string name;
    vsg::vec4   diffuse = vsg::vec4(0.7f, 0.7f, 0.7f, 1.0f);  // Kd
    std::string map_kd;   // map_Kd path token from .mtl (empty = no texture)
};

/// Load a Wavefront .obj file into a vsg::Node.
///
/// @param path     Absolute or resolved path to the .obj file.
/// @param options  VSG options (used for vsg::Builder shader setup
///                 and for resolving any companion files in the
///                 future, e.g. textures).
/// @param out_slots Optional; when non-null it is filled with one
///                 ObjMaterialSlot per root child (material bucket),
///                 in the same order as the returned Group's children.
///                 On the .vsgb cache-hit path the slots come from the
///                 "<obj>.vsgb.slots" sidecar; if that sidecar is
///                 missing the cache is considered stale and the source
///                 is re-parsed (so slots are always consistent with
///                 the returned geometry).
/// @return A vsg::Node containing the triangulated geometry, or
///         null on failure (file not readable, no geometry, etc.).
vsg::ref_ptr<vsg::Node>
loadObjNode(const std::string& path,
            vsg::ref_ptr<vsg::Options> options,
            std::vector<ObjMaterialSlot>* out_slots = nullptr);

} // namespace vsgraysim
