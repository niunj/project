#pragma once
#include "EarthSurface.h"
#include <vsg/nodes/VertexIndexDraw.h>

namespace vsgraysim::earth {
// Both consumers bind the same local positions and indexed topology.
// Display additionally needs normals, texture coordinates and instance color.
vsg::ref_ptr<vsg::VertexIndexDraw> makeSurfaceDraw(const EarthSurfaceSnapshot&, bool shaded);
}
