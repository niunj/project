#include "SubLoadOperation.h"

#include "Device.h"
#include "Image.h"
#include "VulkanBuffer.h"

#include "VulkanInitializers.h"
#include "VulkanDebug.h"
#include "ImageTransitions.h"

namespace SilverLining
{
namespace Vulkan
{
SubLoadOperation::SubLoadOperation(Image* _image)
    : image(_image)
{
    device = const_cast<Device*>(image->GetDevice());

    // staging buffer
    const VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    const VkMemoryPropertyFlags memoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    // at least as big as the image
    const VkDeviceSize sizeInBytes = image->Width() * image->Height() * image->Depth() * 2 * sizeof(std::uint8_t);
    stagingBuffer = new RawBuffer(device
                                  , usageFlags
                                  , memoryPropertyFlags
                                  , sizeInBytes, "");

    stagingBuffer->Map(0, VK_WHOLE_SIZE);
    pDstData = (uint8_t*)stagingBuffer->MappedPtr();

    // command buffer
    commandBuffer = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);

    // fence
    VkFenceCreateInfo fenceCreateInfo = vkInitializers::fenceCreateInfo();
    VK_CHECK_RESULT(vkCreateFence(device->VulkanDevice(), &fenceCreateInfo, nullptr, &fence));
}

SubLoadOperation::~SubLoadOperation()
{
    stagingBuffer->UnMap();
    delete stagingBuffer;
    vkDestroyFence(device->VulkanDevice(), fence, nullptr);
    vkFreeCommandBuffers(device->VulkanDevice(), device->GetCommandPool(), 1, &commandBuffer);
}

void SubLoadOperation::SubLoad(const unsigned char* pSrcData, int width, int height, int depth,
                               int x, int y, int z)
{
    const VkDeviceSize srcDataSize = width * height * depth * 2 * sizeof(std::uint8_t);

    memcpy(pDstData, pSrcData, srcDataSize);

    bufferCopyRegions.clear();

    VkBufferImageCopy bufferImageCopy = {};

    bufferImageCopy.bufferOffset = 0;
    bufferImageCopy.bufferRowLength = 0;
    bufferImageCopy.bufferImageHeight = 0;

    bufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bufferImageCopy.imageSubresource.mipLevel = 0;
    bufferImageCopy.imageSubresource.baseArrayLayer = 0;
    bufferImageCopy.imageSubresource.layerCount = 1;

    bufferImageCopy.imageOffset = { x,y,z };
    bufferImageCopy.imageExtent.width = width;
    bufferImageCopy.imageExtent.height = height;
    bufferImageCopy.imageExtent.depth = depth;

    bufferCopyRegions.push_back(bufferImageCopy);

    VkCommandBufferBeginInfo cmdBufInfo = vkInitializers::commandBufferBeginInfo();
    VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufInfo));

    transitions::setImageLayout(commandBuffer, image->VulkanImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

    vkCmdCopyBufferToImage(commandBuffer
                           , stagingBuffer->VulkanBuffer()
                           , image->VulkanImage()
                           , VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                           , (uint32_t)bufferCopyRegions.size()
                           , bufferCopyRegions.data()
                          );

    transitions::setImageLayout(commandBuffer, image->VulkanImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);

    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));


    VkSubmitInfo submitInfo = vkInitializers::submitInfo();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    // Submit to the queue
    VK_CHECK_RESULT(vkQueueSubmit(device->GraphicsQueue(), 1, &submitInfo, fence));

    // Wait for the fence to signal that command buffer has finished executing
    VK_CHECK_RESULT(vkWaitForFences(device->VulkanDevice(), 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

    vkResetFences(device->VulkanDevice(), 1, &fence);
}
}
}