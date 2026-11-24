#include "vsgraysim/broker/RadarFrameCodec.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace vsgraysim::wire {
namespace {
constexpr uint32_t axisExtension = 0x32524452; // RDR2
constexpr uint32_t receiverExtension = 0x33524452; // RDR3
constexpr uint32_t groundExtension = 0x34524452; // RDR4
constexpr size_t maxPayloadBytes = 16 * 1024 * 1024;
bool littleEndian() { const uint16_t one = 1; return *reinterpret_cast<const uint8_t*>(&one) == 1; }
struct Writer {
    std::vector<uint8_t> data;
    template<class T> void put(T value) {
        static_assert(std::is_arithmetic_v<T>);
        std::array<uint8_t, sizeof(T)> bytes;
        std::memcpy(bytes.data(), &value, sizeof(T));
        if (!littleEndian()) std::reverse(bytes.begin(), bytes.end());
        data.insert(data.end(), bytes.begin(), bytes.end());
    }
};
struct Reader {
    const uint8_t* data;
    size_t size, offset = 0;
    bool ok = true;
    template<class T> T get() {
        if (offset > size || sizeof(T) > size - offset) { ok = false; return {}; }
        std::array<uint8_t, sizeof(T)> bytes;
        std::memcpy(bytes.data(), data + offset, sizeof(T)); offset += sizeof(T);
        if (!littleEndian()) std::reverse(bytes.begin(), bytes.end());
        T value; std::memcpy(&value, bytes.data(), sizeof(T)); return value;
    }
};
}

std::vector<uint8_t> encodeRadarFrame(const RadarFrame& f) {
    const uint64_t cells = uint64_t(f.range_bins) * f.doppler_bins;
    if(f.ground&&f.ground->size()>RadarFrame::maxGroundSamples)
        throw std::invalid_argument("oversized radar ground layer");
    const size_t groundBytes=f.ground?8+12*f.ground->size():0;
    if (!cells || cells != f.rd_map.size() || cells > (maxPayloadBytes - 256 - groundBytes) / 8)
        throw std::invalid_argument("invalid radar frame payload size");
    Writer w;
    w.put(f.frame_id); w.put(f.sim_time); w.put(f.timestamp_ns);
    w.put(f.sensor_id); w.put(f.platform_id);
    w.put(f.carrier_hz); w.put(f.prf); w.put(f.pulse_width); w.put(f.bandwidth);
    w.put(f.coherent_pulses); w.put(uint32_t(f.pol));
    w.put(f.range_min); w.put(f.range_max);
    w.put(f.range_resolution); w.put(f.velocity_resolution);
    w.put(f.latitude); w.put(f.longitude); w.put(f.altitude);
    w.put(f.roll); w.put(f.pitch); w.put(f.yaw);
    w.put(f.velocity_n); w.put(f.velocity_e); w.put(f.velocity_d);
    w.put(f.range_bins); w.put(f.doppler_bins);
    w.put(f.snr_db); w.put(f.quality_flags); w.put(f.stage);
    w.put(uint32_t(cells));
    for (auto sample : f.rd_map) { w.put(sample.real()); w.put(sample.imag()); }
    w.put(axisExtension); w.put(f.beam_az_deg); w.put(f.sample_rate_hz);
    if(f.receiver){
        if(!f.receiver->valid()||f.receiver->first_transmit_s>f.sim_time)
            throw std::invalid_argument("invalid radar receiver timing metadata");
        w.put(receiverExtension);w.put(f.receiver->first_transmit_s);w.put(f.receiver->noise_iq_rms);
    }
    if(f.ground){
        w.put(groundExtension);w.put(uint32_t(f.ground->size()));
        for(const auto& point:*f.ground){
            if(!point.valid())throw std::invalid_argument("invalid radar ground sample");
            w.put(point.bearing_deg);w.put(point.slant_range_m);w.put(point.power_relative);
        }
    }
    return std::move(w.data);
}

bool decodeRadarFrame(const void* data, size_t size, RadarFrame& out) {
    if (!data || !size || size > maxPayloadBytes) return false;
    Reader r{static_cast<const uint8_t*>(data), size};
    RadarFrame f;
    f.frame_id = r.get<uint64_t>(); f.sim_time = r.get<double>();
    f.timestamp_ns = r.get<uint64_t>();
    f.sensor_id = r.get<uint32_t>(); f.platform_id = r.get<uint32_t>();
    f.carrier_hz = r.get<float>(); f.prf = r.get<float>();
    f.pulse_width = r.get<float>(); f.bandwidth = r.get<float>();
    f.coherent_pulses = r.get<uint32_t>();
    const auto polarization = r.get<uint32_t>();
    if (polarization > uint32_t(Polarization::VH)) return false;
    f.pol = Polarization(polarization);
    f.range_min = r.get<float>(); f.range_max = r.get<float>();
    f.range_resolution = r.get<float>(); f.velocity_resolution = r.get<float>();
    f.latitude = r.get<double>(); f.longitude = r.get<double>(); f.altitude = r.get<double>();
    f.roll = r.get<float>(); f.pitch = r.get<float>(); f.yaw = r.get<float>();
    f.velocity_n = r.get<float>(); f.velocity_e = r.get<float>(); f.velocity_d = r.get<float>();
    f.range_bins = r.get<uint32_t>(); f.doppler_bins = r.get<uint32_t>();
    f.snr_db = r.get<float>(); f.quality_flags = r.get<uint32_t>(); f.stage = r.get<uint8_t>();
    const uint64_t cells = uint64_t(f.range_bins) * f.doppler_bins;
    const auto count = r.get<uint32_t>();
    if (!r.ok || !cells || count != cells || cells > (r.size - r.offset) / 8) return false;
    f.rd_map.resize(size_t(cells));
    for (auto& sample : f.rd_map) {
        const auto re = r.get<float>(); const auto im = r.get<float>(); sample = {re, im};
    }
    if (r.offset != r.size) {
        if (r.size - r.offset < 16 || r.get<uint32_t>() != axisExtension) return false;
        f.beam_az_deg = r.get<float>(); f.sample_rate_hz = r.get<double>();
    }
    if(r.offset!=r.size){
        std::optional<uint32_t> extension=r.get<uint32_t>();
        if(extension==receiverExtension){
            f.receiver=RadarFrame::Receiver{r.get<double>(),r.get<double>()};
            if(!r.ok||!f.receiver->valid()||f.receiver->first_transmit_s>f.sim_time)return false;
            if(r.offset==r.size)extension.reset();
            else extension=r.get<uint32_t>();
        }
        if(extension){
            if(*extension!=groundExtension)return false;
            const auto count=r.get<uint32_t>();
            if(!r.ok||count>RadarFrame::maxGroundSamples||r.size-r.offset!=size_t(count)*12)return false;
            f.ground.emplace();f.ground->reserve(count);
            for(uint32_t i=0;i<count;++i){
                RadarFrame::GroundSample point{r.get<float>(),r.get<float>(),r.get<float>()};
                if(!point.valid())return false;
                f.ground->push_back(point);
            }
        }
    }
    if (!r.ok || !std::isfinite(f.sample_rate_hz) || f.sample_rate_hz < 0 ||
        !std::isfinite(f.beam_az_deg)) return false;
    out = std::move(f);
    return true;
}
} // namespace vsgraysim::wire
