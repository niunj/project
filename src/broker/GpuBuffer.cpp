#include "vsgraysim/broker/GpuBuffer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <spdlog/spdlog.h>

namespace vsgraysim {

namespace {
bool g_gpu_probe_done = false;
bool g_gpu_available  = false;
/// H2D/D2H 拷贝失败一次即置位: 设备传输通道不可靠时全局放弃 CUDA 分配,
/// 后续所有缓冲区走主机内存路径 (避免每帧反复失败丢帧)。

/// 一次性探测 CUDA 设备 (失败后不再重复尝试, 避免刷日志)
bool cudaAvailable()
{
    if (!g_gpu_probe_done) {
        g_gpu_probe_done = true;
        int dev_count = 0;
        cudaError_t err = cudaGetDeviceCount(&dev_count);
        g_gpu_available = (err == cudaSuccess && dev_count > 0);
        if (!g_gpu_available)
            spdlog::warn("GpuBuffer: no CUDA device, falling back to host memory");
    }
    return g_gpu_available;
}

bool g_gpu_copy_broken = false;
} // anonymous namespace

GpuBuffer::~GpuBuffer() { release(); }

void GpuBuffer::release()
{
    if (dev_) { cudaFree(dev_); dev_ = nullptr; }
    if (host_) { std::free(host_); host_ = nullptr; }
    capacity_ = 0;
    valid_ = 0;
}

bool GpuBuffer::reserve(size_t bytes)
{
    if (bytes == 0) bytes = 1;
    if (bytes <= capacity_ && (dev_ || host_)) return true;

    // 容量按 2 的幂向上取整, 减少反复分配
    size_t cap = 1;
    while (cap < bytes) cap <<= 1;

    if (!g_gpu_copy_broken && cudaAvailable()) {
        void* nd = nullptr;
        if (cudaMalloc(&nd, cap) != cudaSuccess) {
            spdlog::warn("GpuBuffer: cudaMalloc({}) failed: {} — host fallback",
                         cap, cudaGetErrorString(cudaGetLastError()));
        } else {
            if (dev_) cudaFree(dev_);
            dev_ = nd;
            capacity_ = cap;
            valid_ = 0;
            return true;
        }
    }

    // 主机后备
    void* nh = std::realloc(host_, cap);
    if (!nh) return false;
    host_ = nh;
    capacity_ = cap;
    valid_ = 0;
    return true;
}

bool GpuBuffer::upload(const void* host, size_t bytes)
{
    if (!host || bytes == 0) return false;
    if (!reserve(bytes)) return false;
    if (dev_) {
        if (cudaMemcpy(dev_, host, bytes, cudaMemcpyHostToDevice) != cudaSuccess) {
            // 显存已分配但传输失败 (驱动/环境不支持等): 释放设备缓冲并全局
            // 降级主机内存, 保证本帧数据仍可经 download()/memcpy 路径送达,
            // 而不是每帧返回 false 把整帧丢掉。
            spdlog::warn("GpuBuffer: H2D copy failed: {} — disabling CUDA path, "
                         "falling back to host memory",
                         cudaGetErrorString(cudaGetLastError()));
            cudaFree(dev_);
            dev_ = nullptr;
            g_gpu_copy_broken = true;
        }
    }
    if (dev_) {
        valid_ = bytes;
        return true;
    }

    // 主机后备: capacity_ 可能刚才是为设备内存分配的, host_ 尚未建立
    if (!host_ || capacity_ < bytes) {
        size_t cap = std::max<size_t>(capacity_, 1);
        while (cap < bytes) cap <<= 1;
        void* nh = std::realloc(host_, cap);
        if (!nh) return false;
        host_ = nh;
        capacity_ = cap;
    }
    std::memcpy(host_, host, bytes);
    valid_ = bytes;
    return true;
}

bool GpuBuffer::uploadDevice(const void* dev_src, size_t bytes)
{
    if (!dev_src || bytes == 0) return false;
    if (!reserve(bytes)) return false;
    if (dev_) {
        if (cudaMemcpy(dev_, dev_src, bytes, cudaMemcpyDeviceToDevice) != cudaSuccess) {
            spdlog::warn("GpuBuffer: D2D copy failed: {} — disabling CUDA path",
                         cudaGetErrorString(cudaGetLastError()));
            cudaFree(dev_);
            dev_ = nullptr;
            g_gpu_copy_broken = true;
        } else {
            valid_ = bytes;
            return true;
        }
    }
    // 主机后备: D2H 落入后备缓冲 (调用方持有设备指针, CUDA 运行时必然可用)
    if (!host_ || capacity_ < bytes) {
        size_t cap = std::max<size_t>(capacity_, 1);
        while (cap < bytes) cap <<= 1;
        void* nh = std::realloc(host_, cap);
        if (!nh) return false;
        host_ = nh;
        capacity_ = cap;
    }
    if (cudaMemcpy(host_, dev_src, bytes, cudaMemcpyDeviceToHost) != cudaSuccess)
        return false;
    valid_ = bytes;
    return true;
}

bool GpuBuffer::download(void* host, size_t bytes) const
{
    if (!host || bytes == 0 || bytes > capacity_) return false;
    if (dev_) {
        if (cudaMemcpy(host, dev_, bytes, cudaMemcpyDeviceToHost) != cudaSuccess) {
            // 设备读回失败同样视为传输通道损坏: 后续上传不再走 CUDA
            spdlog::warn("GpuBuffer: D2H copy failed: {} — disabling CUDA path",
                         cudaGetErrorString(cudaGetLastError()));
            g_gpu_copy_broken = true;
            return false;
        }
    } else {
        std::memcpy(host, host_, bytes);
    }
    return true;
}

GpuBufferHandle GpuBuffer::handle() const
{
    GpuBufferHandle h;
    h.size_bytes = capacity_;
    if (dev_) {
        h.device_ptr = dev_;
        h.ipc_valid = (cudaIpcGetMemHandle(&h.ipc, dev_) == cudaSuccess);
    }
    return h;
}

} // namespace vsgraysim
