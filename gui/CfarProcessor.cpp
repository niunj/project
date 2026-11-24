#include "CfarProcessor.h"

#include <algo/factory/AlgoFactory.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vsgraysim { namespace gui {

// 自动注册到算法工厂
namespace {
const auto _reg_cfar = algo::AutoRegister<CfarProcessor>("radar", "cfar_processor");
}

using Alg = CfarProcessor::Algorithm;

float CfarProcessor::noiseLevel(const std::vector<float>& rd, uint32_t w,
                                uint32_t h, int row, int col) const {
    const int g = params_.guard_bins;
    const int r = params_.ref_bins;
    const int rad = g + r;
    std::vector<float> refs;
    refs.reserve(size_t(8 * r));

    for (int dy = -rad; dy <= rad; ++dy) {
        for (int dx = -rad; dx <= rad; ++dx) {
            const int adx = std::abs(dx), ady = std::abs(dy);
            // 环形参考窗: |d| ∈ (guard, guard+ref] 的某一维
            if (std::max(adx, ady) > rad) continue;
            if (std::max(adx, ady) <= g) continue;
            const int y = row + dy, x = col + dx;
            if (y < 0 || y >= int(h) || x < 0 || x >= int(w)) continue;
            refs.push_back(rd[size_t(y) * w + size_t(x)]);
        }
    }
    if (refs.empty()) return -120.0f;

    switch (params_.algo) {
    case Alg::CA: {
        double s = 0; for (float v : refs) s += std::pow(10.,double(v)*.1);
        return float(10*std::log10(std::max(s/refs.size(),1e-24)));
    }
    case Alg::SO: {  // 取较小的一半窗的均值 (左半 vs 右半)
        double left = 0; int nl = 0, right = 0, nr = 0;
        for (int dy = -rad; dy <= rad; ++dy)
            for (int dx = -rad; dx <= rad; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) <= g) continue;
                const int y = row + dy, x = col + dx;
                if (y < 0 || y >= int(h) || x < 0 || x >= int(w)) continue;
                const float v = rd[size_t(y) * w + size_t(x)];
                if (dx < 0 || dy < 0) { left += std::pow(10.,double(v)*.1); ++nl; }
                else                  { right += std::pow(10.,double(v)*.1); ++nr; }
            }
        const double l = nl ? left / nl : std::numeric_limits<double>::infinity();
        const double rr = nr ? right / nr : std::numeric_limits<double>::infinity();
        return float(10*std::log10(std::max(std::min(l, rr),1e-24)));
    }
    case Alg::GO: {  // 同 SO 但取较大均值
        double left = 0; int nl = 0, right = 0, nr = 0;
        for (int dy = -rad; dy <= rad; ++dy)
            for (int dx = -rad; dx <= rad; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) <= g) continue;
                const int y = row + dy, x = col + dx;
                if (y < 0 || y >= int(h) || x < 0 || x >= int(w)) continue;
                const float v = rd[size_t(y) * w + size_t(x)];
                if (dx < 0 || dy < 0) { left += std::pow(10.,double(v)*.1); ++nl; }
                else                  { right += std::pow(10.,double(v)*.1); ++nr; }
            }
        const double l = nl ? left / nl : 0.;
        const double rr = nr ? right / nr : 0.;
        return float(10*std::log10(std::max(std::max(l, rr),1e-24)));
    }
    case Alg::OS: {
        std::vector<float> sorted = refs;
        std::sort(sorted.begin(), sorted.end());
        const int k = std::clamp(params_.os_rank, 0, int(sorted.size()) - 1);
        return sorted[size_t(k)];
    }
    }
    return -120.0f;
}

std::vector<CfarProcessor::Detection>
CfarProcessor::detect(const std::vector<float>& rd_db, uint32_t w,
                      uint32_t h) const {
    std::vector<Detection> out;
    if (rd_db.size() != size_t(w) * h || w == 0 || h == 0) return out;

    last_cell_count = 0;
    float peak=-240;for(float v:rd_db)if(std::isfinite(v))peak=std::max(peak,v);
    const float floor=peak-std::max(0.f,params_.max_dynamic_range_db);
    std::vector<Detection> cells;
    for (uint32_t row = 0; row < h; ++row) {
        for (uint32_t col = 0; col < w; ++col) {
            const float v = rd_db[size_t(row) * w + size_t(col)];
            if(!std::isfinite(v)||v<floor)continue;
            const float noise = noiseLevel(rd_db, w, h, int(row), int(col));
            const float snr = v - noise;
            if (snr >= params_.threshold_db && snr >= params_.min_snr_db) {
                cells.push_back({int(row), int(col), snr});
            }
        }
    }
    last_cell_count = int(cells.size());
    cluster(cells, w, h, out);
    return out;
}

void CfarProcessor::cluster(const std::vector<Detection>& cells, uint32_t w,
                            uint32_t h, std::vector<Detection>& out) const {
    const int n = int(cells.size());
    if (n == 0) return;
    const int R = params_.cluster_radius;

    std::vector<int> label(n, -1);
    std::vector<std::vector<int>> groups;
    for (int i = 0; i < n; ++i) {
        if (label[i] >= 0) continue;
        std::vector<int> stack{i};
        label[i] = int(groups.size());
        std::vector<int> group{i};
        while (!stack.empty()) {
            const int c = stack.back(); stack.pop_back();
            for (int j = 0; j < n; ++j) {
                if (label[j] >= 0) continue;
                if (std::abs(cells[j].row - cells[c].row) <= R &&
                    std::abs(cells[j].col - cells[c].col) <= R) {
                    label[j] = label[i];
                    group.push_back(j);
                    stack.push_back(j);
                }
            }
        }
        groups.push_back(std::move(group));
    }

    (void)w; (void)h;
    out.reserve(groups.size());
    for (const auto& g : groups) {
        double sr = 0, sc = 0, snr = 0;
        for (int idx : g) {
            sr += cells[idx].row; sc += cells[idx].col;
            snr = std::max(snr, double(cells[idx].snr_db));
        }
        out.push_back({int(sr / g.size()), int(sc / g.size()), float(snr)});
    }
}

}} // namespace vsgraysim::gui
