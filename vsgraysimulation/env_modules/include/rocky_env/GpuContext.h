#pragma once
#include <vsg/core/ref_ptr.h>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include <vsg/core/ref_ptr.h>
namespace vsg { class Device; class PhysicalDevice; class DeviceFeatures; class Node; class CommandGraph; class Buffer; class ImageView; }
namespace rockyenv::sensor {
/// One synchronous, window/surface/swapchain-free Vulkan execution domain.
/// Submission waits for its fence before buffers or scene instances are reused.
class GpuContext {
public:
    GpuContext();
    ~GpuContext();
    void initialize(bool validation=false,vsg::Device* shared_device=nullptr);
    /// Call before host device creation to enable supported sensor features.
    static void configureDevice(vsg::PhysicalDevice&,std::vector<const char*>& extensions,vsg::ref_ptr<vsg::DeviceFeatures>& features);
    vsg::Device* device() const;
    bool rayTracing() const;
    bool radarSupported() const;
    const std::string& deviceName() const;
    void compile(vsg::ref_ptr<vsg::CommandGraph> graph);
    void execute(vsg::ref_ptr<vsg::CommandGraph> graph);
    // Callers freeze all shared resources until completion. Separate sensor
    // outputs/UBOs remain independent; incompatible truth must flush first.
    void executeBatch(const std::vector<vsg::ref_ptr<vsg::CommandGraph>>& graphs);
    struct Readback {
        vsg::ImageView* image=nullptr;
        vsg::Buffer* buffer=nullptr;
        VkImageLayout layout=VK_IMAGE_LAYOUT_GENERAL;
        VkDeviceSize bytes=0;
        void* destination=nullptr;
    };
    void readbackBatch(const std::vector<Readback>&);
    uint64_t executionSubmissions()const;
    void waitIdle();
    void clearGraphs();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
