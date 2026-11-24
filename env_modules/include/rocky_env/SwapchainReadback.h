#pragma once
#include <vsg/app/Window.h>
#include <vsg/commands/CopyImageToBuffer.h>
#include <vsg/commands/PipelineBarrier.h>

namespace rockyenv {
// Record after the render graph, before presentation releases the acquired image.
struct SwapchainReadback : public vsg::Inherit<vsg::Command, SwapchainReadback>
{
    vsg::ref_ptr<vsg::Window> window;
    vsg::ref_ptr<vsg::Buffer> dstBuffer;
    bool* request = nullptr;   // 主循环同线程置位, record 内消费

    mutable VkExtent2D recordedExtent{};

    void record(vsg::CommandBuffer& cb) const override
    {
        if (!request || !*request || !window || !dstBuffer) return;
        auto sc = window->getSwapchain();
        if (!sc) return;
        const VkExtent2D extent = sc->getExtent();
        if (VkDeviceSize(extent.width) * extent.height * 4 > dstBuffer->size) return;
        auto image = window->imageView(window->imageIndex());
        if (!image || !image->image) return;

        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        auto toSrc = vsg::ImageMemoryBarrier::create(
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image->image, range);
        auto back = vsg::ImageMemoryBarrier::create(
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image->image, range);

        auto toSrcB = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
        toSrcB->add(toSrc);
        toSrcB->record(cb);

        auto copy = vsg::CopyImageToBuffer::create();
        copy->srcImage = image->image;
        copy->srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copy->dstBuffer = dstBuffer;
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        const VkExtent2D e2 = sc->getExtent();
        region.imageExtent = {e2.width, e2.height, 1};
        copy->regions.push_back(region);
        copy->record(cb);

        auto backB = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0);
        backB->add(back);
        backB->record(cb);
        recordedExtent = extent;
    }
};

} // namespace rockyenv
