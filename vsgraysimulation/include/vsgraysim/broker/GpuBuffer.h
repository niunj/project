#pragma once
/// @file GpuBuffer.h
/// @brief GPU 缓冲区: CUDA 显存 RAII 封装, 支持 IPC 句柄导出 + 主机后备。
///
/// 仿真控制软件(数据生成方)把装备数据(雷达/SAR 回波、红外辐射亮度)写入
/// GPU 缓冲区, 通过 cudaIpcMemHandle_t 句柄传递给各显控软件(数据处理方)。
/// - 同进程: 直接用 devicePtr() 零拷贝访问
/// - 跨进程: 通过 exportIpcHandle() / openIpcHandle() 传递
/// - 无 CUDA 设备时自动退化为主机内存 (onGpu() == false)

#include <cstddef>
#include <cstdint>

#include <cuda_runtime.h>

namespace vsgraysim {

/// GPU 句柄: 跨软件传递的设备缓冲区标识
struct GpuBufferHandle {
    void*             device_ptr = nullptr;  ///< 同进程直接可用的设备指针
    uint64_t          size_bytes = 0;        ///< 缓冲区容量
    cudaIpcMemHandle_t ipc{};                ///< CUDA IPC 句柄 (跨进程用)
    bool              ipc_valid = false;     ///< IPC 句柄是否有效
};

class GpuBuffer {
public:
    GpuBuffer() = default;
    ~GpuBuffer();

    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    /// 确保容量 >= bytes (缩小或首次分配时重新分配)
    bool reserve(size_t bytes);

    /// 主机内存 -> GPU (或主机后备内存 memcpy)。bytes 可小于容量。
    bool upload(const void* host, size_t bytes);

    /// 设备内存 -> GPU 缓冲区 (D2D 拷贝, 帧本体不落主机);
    /// 无 CUDA 设备时降级为 D2H 写入主机后备。
    bool uploadDevice(const void* dev_src, size_t bytes);

    /// GPU -> 主机内存
    bool download(void* host, size_t bytes) const;

    /// 导出可传递的 GPU 句柄
    GpuBufferHandle handle() const;

    void*   devicePtr() const { return dev_; }     ///< nullptr 表示主机后备
    void*   hostFallback() const { return host_; } ///< 主机后备缓冲
    size_t  capacity() const { return capacity_; }
    size_t  validBytes() const { return valid_; }
    bool    onGpu() const { return dev_ != nullptr; }

private:
    void release();

    void*  dev_     = nullptr;  ///< cudaMalloc 设备指针
    void*  host_    = nullptr;  ///< 主机后备 (无 GPU 或下载暂存)
    size_t capacity_ = 0;
    size_t valid_    = 0;
};

} // namespace vsgraysim
