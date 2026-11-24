#include "VoxelTerrain.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vsgraysim/earth/VoxelEarthPatch.h>
namespace vsgraysim::earth {
VoxelEarthPatch::Sculpt VoxelEarthPatch::sculpt(std::shared_ptr<const VoxelTerrain> volume,
                                                const nlohmann::json &brush) const {
    if (volume->dimensions() != std::array<int, 3>{nx_, ny_, nz_} ||
        volume->gridOrigin() != vsg::dvec3(x_, bottom_, z_) || volume->resolution_m != cell_)
        throw std::invalid_argument("sculpt requires the original complete Earth working grid");
    const auto center = brush.at("center_m");
    if (!center.is_array() || center.size() != 3)
        throw std::invalid_argument("terrain brush requires local XYZ");
    const double cx = center[0].get<double>(), cz = center[2].get<double>(), radius = brush.at("radius_m"),
                 amount = brush.value("amount_m", 1.), strength = brush.value("strength", 1.);
    if (!std::isfinite(cx) || !std::isfinite(center[1].get<double>()) || !std::isfinite(cz) ||
        !std::isfinite(radius) || radius <= 0 || radius > 1e6 || !std::isfinite(amount) || amount < 0 ||
        amount > 1e5 || !std::isfinite(strength) || strength <= 0 || strength > 1)
        throw std::invalid_argument("invalid terrain brush range/strength");
    const auto operation = brush.at("operation").get<std::string>();
    if (operation != "raise" && operation != "lower" && operation != "flatten" && operation != "smooth")
        throw std::invalid_argument("unsupported terrain brush operation");
    const double target = brush.value("height_m", bottom_);
    if (!std::isfinite(target))
        throw std::invalid_argument("invalid terrain flatten height");
    auto patch = std::shared_ptr<VoxelEarthPatch>(new VoxelEarthPatch(*this));
    const int pitch = nx_ * sub_ + 1, rows = nz_ * sub_ + 1;
    bool changed = false;
    for (int z = 0; z < rows; ++z)
        for (int x = 0; x < pitch; ++x) {
            const double d = std::hypot(x_ + x * cell_ / sub_ - cx, z_ + z * cell_ / sub_ - cz) / radius;
            if (d >= 1)
                continue;
            const size_t i = size_t(z) * pitch + x;
            double requested = heights_[i];
            const double weight = (1 - d * d) * (1 - d * d) * strength;
            if (operation == "raise")
                requested += amount * weight;
            else if (operation == "lower")
                requested -= amount * weight;
            else if (operation == "flatten")
                requested += (target - requested) * weight;
            else {
                double sum = 0;
                int count = 0;
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, zz = z + dz;
                        if (xx >= 0 && xx < pitch && zz >= 0 && zz < rows) {
                            sum += heights_[size_t(zz) * pitch + xx];
                            ++count;
                        }
                    }
                requested += (sum / count - requested) * weight;
            }
            if (requested <= bottom_ || requested > bottom_ + ny_ * cell_)
                throw std::invalid_argument("terrain brush exceeds the prepared vertical working grid; "
                                            "expand the source working set without coarsening cells");
            changed = changed || requested != heights_[i];
            patch->heights_[i] = requested;
        }
    if (!changed)
        throw std::invalid_argument("terrain brush did not intersect/change this working set");
    auto occupied = volume->occupancy();
    for (int z = 0; z < nz_; ++z)
        for (int x = 0; x < nx_; ++x) {
            double before = bottom_, after = bottom_, beforeLand = -1e30, afterLand = -1e30;
            for (int dz = 0; dz <= sub_; ++dz)
                for (int dx = 0; dx <= sub_; ++dx) {
                    const auto i = size_t(z * sub_ + dz) * pitch + x * sub_ + dx;
                    before = std::max(before, heights_[i]);
                    after = std::max(after, patch->heights_[i]);
                    if (!seaReference_.empty()) {
                        beforeLand = std::max(beforeLand, heights_[i] - seaReference_[i]);
                        afterLand = std::max(afterLand, patch->heights_[i] - seaReference_[i]);
                    }
                }
            for (int y = 0; y < ny_; ++y) {
                const bool oldSolid = (seaReference_.empty() || beforeLand > seaMaskEpsilon_) &&
                                      bottom_ + y * cell_ < before - 1e-10,
                           newSolid = (seaReference_.empty() || afterLand > seaMaskEpsilon_) &&
                                      bottom_ + y * cell_ < after - 1e-10;
                if (oldSolid != newSolid)
                    occupied[(size_t(z) * ny_ + y) * nx_ + x] =
                        newSolid; // preserve existing damage wherever the authored base is unchanged
            }
        }
    patch->validate();
    patch->provenance_["authored_height_edits"] = true;
    patch->provenance_["height_edit_source"] = "user";
    patch->provenance_["last_height_brush"] = brush;
    return {patch, VoxelTerrain::fromOccupancy({x_, bottom_, z_}, cell_, nx_, ny_, nz_, occupied, false)};
}
} // namespace vsgraysim::earth
