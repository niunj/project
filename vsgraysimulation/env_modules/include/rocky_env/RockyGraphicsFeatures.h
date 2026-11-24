#pragma once
#include <vsg/vk/PhysicalDevice.h>
#include <vsg/vk/DeviceFeatures.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace rockyenv {
/// Installed Rocky terrain decals declare non-uniform sampled-image indexing.
/// Call for the selected physical device before creating the logical device.
/// Unsupported devices fail explicitly instead of compiling an illegal shader.
inline void configureRockyGraphicsFeatures(vsg::PhysicalDevice& physical,
        std::vector<const char*>& extensions,vsg::ref_ptr<vsg::DeviceFeatures>& enabled) {
    const auto supported=physical.getFeatures<VkPhysicalDeviceDescriptorIndexingFeatures,
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES>();
    if(!supported.shaderSampledImageArrayNonUniformIndexing ||
       !physical.supportsDeviceExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME))
        throw std::runtime_error("Rocky terrain requires supported sampled-image non-uniform indexing (VK_EXT_descriptor_indexing)");
    if(!enabled)enabled=vsg::DeviceFeatures::create();
    enabled->get<VkPhysicalDeviceDescriptorIndexingFeatures,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES>()
        .shaderSampledImageArrayNonUniformIndexing=VK_TRUE;
    if(std::none_of(extensions.begin(),extensions.end(),[](const char* name){return std::strcmp(name,VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME)==0;}))
        extensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
}
}
