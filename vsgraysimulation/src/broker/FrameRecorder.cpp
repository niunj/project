#include "vsgraysim/broker/FrameRecorder.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstring>

#include <spdlog/spdlog.h>

namespace vsgraysim {

namespace {

constexpr char kMagic[4] = {'V', 'S', 'R', 'D'};
constexpr uint32_t kVersion = 3; // v3 also stores the pinned Earth revision

#pragma pack(push, 1)
struct FileHeader {
    char     magic[4];
    uint32_t version;
    uint64_t frame_count;
};

struct FrameHead {
    uint64_t frame_id;
    double   sim_time;
    int32_t  sensor_type;
    uint32_t sensor_id;
    uint32_t width;
    uint32_t height;
    float    fs;
    float    prf;
    double   carrier_hz;
    uint32_t coherent_pulses;
    float    range_min;
    float    range_max;
    uint32_t n_samples;
    uint32_t n_range;
    uint32_t n_doppler;
};
#pragma pack(pop)

static_assert(sizeof(FileHeader) == 16, "FileHeader layout");
static_assert(sizeof(FrameHead) == 72, "FrameHead layout");

/// 递归创建目录 (start() 用, 目录不存在时自动创建)
void mkDirs(const std::string& path)
{
    if (path.empty()) return;
    std::string cur;
    size_t pos = 0;
    while (pos < path.size()) {
        const size_t next = path.find('/', pos);
        cur = (next == std::string::npos) ? path : path.substr(0, next);
        if (!cur.empty() && cur != "/" && cur != ".")
            ::mkdir(cur.c_str(), 0755); // 已存在则忽略
        if (next == std::string::npos) break;
        pos = next + 1;
    }
}

/// 从路径提取父目录
std::string parentDir(const std::string& path)
{
    const auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? std::string() : path.substr(0, pos);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// FrameRecorder
// ---------------------------------------------------------------------------

FrameRecorder::~FrameRecorder()
{
    stop();
}

bool FrameRecorder::start(const std::string& file_path)
{
    if (out_.is_open()) stop();
    if (file_path.empty()) return false;

    const std::string dir = parentDir(file_path);
    if (!dir.empty()) mkDirs(dir);

    out_.open(file_path, std::ios::binary | std::ios::trunc);
    if (!out_.is_open()) {
        spdlog::error("FrameRecorder: cannot open '{}' ({})", file_path,
                      std::strerror(errno));
        return false;
    }

    // 文件头占位 (帧计数回填)
    FileHeader hdr{};
    std::memcpy(hdr.magic, kMagic, 4);
    hdr.version = kVersion;
    hdr.frame_count = 0;
    out_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    header_pos_ = 0;

    path_ = file_path;
    frame_count_ = 0;
    spdlog::info("FrameRecorder: start '{}'", file_path);
    return true;
}

bool FrameRecorder::recordFrame(const SignalFrame& frame)
{
    if (!out_.is_open() || frame.empty()) return false;

    FrameHead h{};
    h.frame_id = frame.frame_id;
    h.sim_time = frame.sim_time;
    h.sensor_type = int32_t(frame.sensor_type);
    h.sensor_id = frame.sensor_id;
    h.width = frame.width;
    h.height = frame.height;
    h.fs = frame.fs;
    h.prf = frame.prf;
    h.carrier_hz = frame.carrier_hz;
    h.coherent_pulses = frame.coherent_pulses;
    h.range_min = frame.range_min;
    h.range_max = frame.range_max;
    h.n_samples = uint32_t(frame.samples.size());
    h.n_range = uint32_t(frame.range_axis.size());
    h.n_doppler = uint32_t(frame.doppler_axis.size());

    out_.write(reinterpret_cast<const char*>(&h), sizeof(h));
    out_.write(reinterpret_cast<const char*>(&frame.product_stage),1);
    out_.write(reinterpret_cast<const char*>(&frame.surface_revision),sizeof(frame.surface_revision));
    if (h.n_samples > 0)
        out_.write(reinterpret_cast<const char*>(frame.samples.data()),
                   std::streamsize(sizeof(std::complex<float>) * h.n_samples));
    if (h.n_range > 0)
        out_.write(reinterpret_cast<const char*>(frame.range_axis.data()),
                   std::streamsize(sizeof(float) * h.n_range));
    if (h.n_doppler > 0)
        out_.write(reinterpret_cast<const char*>(frame.doppler_axis.data()),
                   std::streamsize(sizeof(float) * h.n_doppler));

    if (!out_.good()) {
        spdlog::error("FrameRecorder: write failed at frame {}", frame_count_);
        return false;
    }
    ++frame_count_;
    return true;
}

void FrameRecorder::stop()
{
    if (!out_.is_open()) return;

    // 回填帧计数
    out_.seekp(std::streamoff(header_pos_));
    FileHeader hdr{};
    std::memcpy(hdr.magic, kMagic, 4);
    hdr.version = kVersion;
    hdr.frame_count = frame_count_;
    out_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    out_.close();
    spdlog::info("FrameRecorder: stop '{}' ({} frames)", path_, frame_count_);
}

// ---------------------------------------------------------------------------
// FramePlayer
// ---------------------------------------------------------------------------

uint64_t FramePlayer::open(const std::string& file_path)
{
    close();
    if (file_path.empty()) return 0;

    in_.open(file_path, std::ios::binary);
    if (!in_.is_open()) return 0;

    FileHeader hdr{};
    in_.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in_.good() || std::memcmp(hdr.magic, kMagic, 4) != 0 ||
        (hdr.version < 1 || hdr.version > kVersion)) {
        spdlog::error("FramePlayer: bad file header '{}'", file_path);
        close();
        return 0;
    }

    total_frames_ = hdr.frame_count;
    version_=hdr.version;
    cur_index_ = 0;
    duration_s_ = 0.0;
    path_ = file_path;
    return total_frames_;
}

bool FramePlayer::nextFrame(SignalFrame* out)
{
    if (!in_.is_open() || out == nullptr) return false;

    FrameHead h{};
    in_.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!in_.good()) return false;

    out->product_stage=4; // legacy recordings did not encode product units/stage
    if(version_>=2) {
        in_.read(reinterpret_cast<char*>(&out->product_stage),1);
        if(!in_.good() || out->product_stage>4) return false;
    }
    out->surface_revision=0;
    if(version_>=3 && !in_.read(reinterpret_cast<char*>(&out->surface_revision),sizeof(out->surface_revision))) return false;
    out->frame_id = h.frame_id;
    out->sim_time = h.sim_time;
    out->sensor_type = SensorType(h.sensor_type);
    out->sensor_id = h.sensor_id;
    out->width = h.width;
    out->height = h.height;
    out->fs = h.fs;
    out->prf = h.prf;
    out->carrier_hz = h.carrier_hz;
    out->coherent_pulses = h.coherent_pulses;
    out->range_min = h.range_min;
    out->range_max = h.range_max;

    out->samples.resize(h.n_samples);
    if (h.n_samples > 0)
        in_.read(reinterpret_cast<char*>(out->samples.data()),
                 std::streamsize(sizeof(std::complex<float>) * h.n_samples));
    out->range_axis.resize(h.n_range);
    if (h.n_range > 0)
        in_.read(reinterpret_cast<char*>(out->range_axis.data()),
                 std::streamsize(sizeof(float) * h.n_range));
    out->doppler_axis.resize(h.n_doppler);
    if (h.n_doppler > 0)
        in_.read(reinterpret_cast<char*>(out->doppler_axis.data()),
                 std::streamsize(sizeof(float) * h.n_doppler));

    if (!in_.good()) return false;

    if (out->sim_time > duration_s_) duration_s_ = out->sim_time;
    ++cur_index_;
    return true;
}

bool FramePlayer::seek(uint64_t frame_index)
{
    if (frame_index >= total_frames_) return false;

    // 向后回绕: 重开文件从头扫
    if (frame_index < cur_index_ || !in_.is_open()) {
        const std::string p = path_;
        if (open(p) == 0) return false;
    }

    // 顺序扫描 (块长不定, 无索引)
    while (cur_index_ < frame_index) {
        FrameHead h{};
        in_.read(reinterpret_cast<char*>(&h), sizeof(h));
        if (!in_.good()) return false;
        in_.seekg(std::streamsize((version_>=2 ? 1 : 0) + (version_>=3 ? sizeof(uint64_t) : 0) + sizeof(std::complex<float>) * h.n_samples +
                                  sizeof(float) * h.n_range +
                                  sizeof(float) * h.n_doppler),
                  std::ios::cur);
        if (!in_.good()) return false;
        ++cur_index_;
    }
    return true;
}

void FramePlayer::close()
{
    if (in_.is_open()) in_.close();
    total_frames_ = 0;
    cur_index_ = 0;
    duration_s_ = 0.0;
}

} // namespace vsgraysim
