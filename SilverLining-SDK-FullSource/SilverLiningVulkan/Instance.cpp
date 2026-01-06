// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "Instance.h"
#include "VulkanDebug.h"
#include <iostream>
#include <sstream>

namespace SilverLining
{
namespace Vulkan
{
Instance::Instance(VkInstance _instance)
    : instance(_instance)
{
    EnumeratePhysicalDevices();
}

Instance::~Instance()
{
    // no op
}

VkInstance Instance::VulkanInstance(void) const
{
    return instance;
}

bool Instance::EnumeratePhysicalDevices()
{
    // Physical device
    uint32_t gpuCount = 0;
    // Get number of available physical devices
    VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));
    if (gpuCount == 0) {
        std::cout << "No device with Vulkan support found" << std::endl;
        return false;
    }
    // Enumerate devices
    physicalDevices.resize(gpuCount);
    VkResult err = vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data());
    if (err) {
        std::cout << "Could not enumerate physical devices : \n" << tools::ErrorString(err) << std::endl;
        return false;
    }
    return true;
}

const SL_VECTOR(VkPhysicalDevice)& Instance::PhysicalDevices(void) const
{
    return physicalDevices;
}
}
}