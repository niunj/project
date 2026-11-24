#include "vsgraysim/layer5_vis/RadarScope.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <vsgImGui/imgui.h>
#include <vsgImGui/implot.h>

namespace vsgraysim {

RadarScope::RadarScope() = default;

void RadarScope::setFrame(const SignalFrame& frame) {
    if (frame.samples.empty() || frame.width == 0 || frame.height == 0)
        return;

    w_ = frame.width;
    h_ = frame.height;
    range_max_ = frame.range_max;
    prf_ = frame.prf;
    carrier_hz_ = frame.carrier_hz;
    sensor_id_ = frame.sensor_id;
    sim_time_ = frame.sim_time;
    frame_id_ = frame.frame_id;
    coherent_pulses_ = frame.coherent_pulses;

    rd_db_.resize(size_t(w_) * h_);
    range_profile_.assign(w_, -120.0f);
    doppler_profile_.assign(h_, -120.0f);

    const std::complex<float>* s = frame.samples.data();
    for (uint32_t row = 0; row < h_; ++row) {
        for (uint32_t col = 0; col < w_; ++col) {
            const float mag = std::abs(s[size_t(row) * w_ + col]);
            const float db = 20.0f * std::log10(mag + 1e-6f);
            rd_db_[size_t(row) * w_ + col] = db;
            range_profile_[col] = std::max(range_profile_[col], db);
            doppler_profile_[row] = std::max(doppler_profile_[row], db);
        }
    }

    // Per-bin axes (fall back to linear axes if the frame left them empty).
    range_axis_.resize(w_);
    doppler_axis_.resize(h_);
    for (uint32_t col = 0; col < w_; ++col)
        range_axis_[col] = (range_max_ > 0.0f && frame.range_axis.size() == w_)
                               ? frame.range_axis[col]
                               : range_max_ * float(col) / float(w_ - 1);
    for (uint32_t row = 0; row < h_; ++row)
        doppler_axis_[row] = (prf_ > 0.0f && frame.doppler_axis.size() == h_)
                                 ? frame.doppler_axis[row]
                                 : (prf_ * (float(row) / float(h_ - 1) - 0.5f));

    // Peak bin + physical coordinates.
    peak_db_ = -120.0f;
    peak_row_ = peak_col_ = 0;
    for (uint32_t row = 0; row < h_; ++row) {
        for (uint32_t col = 0; col < w_; ++col) {
            const float db = rd_db_[size_t(row) * w_ + col];
            if (db > peak_db_) {
                peak_db_ = db;
                peak_row_ = row;
                peak_col_ = col;
            }
        }
    }
    peak_range_m_ = (peak_col_ < range_axis_.size()) ? range_axis_[peak_col_] : 0.0f;
    peak_fd_hz_ = (peak_row_ < doppler_axis_.size()) ? doppler_axis_[peak_row_] : 0.0f;
}

bool RadarScope::render() {
    if (!visible_) return false;

    ImGui::SetNextWindowSize(ImVec2(600.0f, 680.0f), ImGuiCond_FirstUseEver);
    bool show = visible_;
    if (!ImGui::Begin("Radar Scope", &show, ImGuiWindowFlags_NoNav)) {
        ImGui::End();
        visible_ = show;
        return visible_;
    }

    // ---- control strip ----
    ImGui::Checkbox("RD map", &show_rd_);
    ImGui::SameLine();
    ImGui::Checkbox("Range profile", &show_range_profile_);
    ImGui::SameLine();
    ImGui::Checkbox("Doppler profile", &show_doppler_profile_);
    ImGui::SameLine();
    ImGui::Checkbox("Auto fit", &auto_fit_);

    if (hasData()) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::DragFloat2("dB", &db_min_, 1.0f, -120.0f, 40.0f, "%.0f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::Combo("cmap", &colormap_, "Deep\0Dark\0Pastel\0Paired\0Viridis\0"
                                         "Plasma\0Hot\0Cool\0Pink\0Jet\0"
                                         "Twilight\0RdBu\0BrBG\0PiYG\0"
                                         "Spectral\0Greys\0\0");
        if (db_min_ > db_max_) std::swap(db_min_, db_max_);
    }

    ImGui::Separator();

    if (!hasData()) {
        ImGui::TextWrapped("No radar echo frame yet. Run the simulation with "
                           "signal processing enabled (sensor type RADAR) to "
                           "populate the RD map.");
        ImGui::End();
        visible_ = show;
        return visible_;
    }

    // ---- statistics line ----
    ImGui::Text("Sensor %u | sim %.3f s | frame %llu | PRF %.1f Hz | "
                "carrier %.3f GHz | lambda %.4f m",
                sensor_id_, sim_time_,
                static_cast<unsigned long long>(frame_id_),
                prf_, carrier_hz_ * 1e-9,
                carrier_hz_ > 0.0 ? float(299792458.0 / carrier_hz_) : 0.0f);
    ImGui::Text("Range %.0f..%.0f m | %u x %u bins | CPI window %u pulses | "
                "peak %.1f dB @ %.1f m / %.1f Hz",
                range_axis_.empty() ? 0.0f : range_axis_.front(),
                range_max_, w_, h_, coherent_pulses_,
                peak_db_, peak_range_m_, peak_fd_hz_);

    ImGui::Spacing();

    // ---- RD heatmap ----
    if (show_rd_) {
        if (ImPlot::BeginPlot("##rdmap", ImVec2(-1.0f, 420.0f),
                              ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect)) {
            ImPlot::SetupAxis(ImAxis_X1, "Range [m]");
            ImPlot::SetupAxis(ImAxis_Y1, "Doppler [Hz]");
            if (auto_fit_)
                ImPlot::SetupAxesLimits(0.0, double(range_max_),
                                        -0.5 * double(prf_), 0.5 * double(prf_),
                                        ImGuiCond_Always);
            ImPlot::PushColormap(ImPlotColormap(colormap_));
            ImPlot::PlotHeatmap("RD [dB]", rd_db_.data(), int(h_), int(w_),
                                double(db_min_), double(db_max_), "%.0f",
                                ImPlotPoint(0.0, -0.5 * double(prf_)),
                                ImPlotPoint(double(range_max_), 0.5 * double(prf_)));
            ImPlot::PopColormap();
            ImPlot::EndPlot();
        }
        ImGui::Spacing();
    }

    // ---- range profile ----
    if (show_range_profile_) {
        if (ImPlot::BeginPlot("Range profile", ImVec2(-1.0f, 140.0f),
                              ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect)) {
            ImPlot::SetupAxis(ImAxis_X1, "Range [m]");
            ImPlot::SetupAxis(ImAxis_Y1, "dB");
            if (auto_fit_)
                ImPlot::SetupAxesLimits(0.0, double(range_max_),
                                        double(db_min_), double(db_max_),
                                        ImGuiCond_Always);
            ImPlot::PlotLine("Range profile", range_axis_.data(),
                             range_profile_.data(), int(range_profile_.size()));
            ImPlot::EndPlot();
        }
        ImGui::Spacing();
    }

    // ---- doppler profile ----
    if (show_doppler_profile_) {
        if (ImPlot::BeginPlot("Doppler profile", ImVec2(-1.0f, 140.0f),
                              ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect)) {
            ImPlot::SetupAxis(ImAxis_X1, "Doppler [Hz]");
            ImPlot::SetupAxis(ImAxis_Y1, "dB");
            if (auto_fit_)
                ImPlot::SetupAxesLimits(-0.5 * double(prf_), 0.5 * double(prf_),
                                        double(db_min_), double(db_max_),
                                        ImGuiCond_Always);
            ImPlot::PlotLine("Doppler profile", doppler_axis_.data(),
                             doppler_profile_.data(), int(doppler_profile_.size()));
            ImPlot::EndPlot();
        }
    }

    ImGui::End();
    visible_ = show;
    return visible_;
}

} // namespace vsgraysim