#include "SLAssert.h"
#include "FrameIndexedBuffer.h"
#include "VulkanBuffer.h"
#include "Device.h"

namespace SilverLining
{
namespace Vulkan
{
FrameIndexedBuffer::FrameIndexedBuffer(const SL_VECTOR(Buffer*)& _buffers, uint64_t _offsetSizeInBytes)
    : buffers(_buffers)
    , offsetSizeInBytes(_offsetSizeInBytes)
{
    SL_ASSERT(buffers.size()>0);

    if (offsetSizeInBytes > 0) {
        for (auto buffer : buffers) {
            SL_ASSERT(buffer->SizeInBytes() % offsetSizeInBytes == 0 && buffer->SizeInBytes() / offsetSizeInBytes >= 1);
        }
    }
}

FrameIndexedBuffer::~FrameIndexedBuffer()
{

}

uint64_t FrameIndexedBuffer::OffsetSizeInBytes(void) const
{
    return offsetSizeInBytes;
}

void FrameIndexedBuffer::IncrementOffsetIndex()
{
    ++offsetIndex;
    // all buffers are same size
    if (offsetIndex * offsetSizeInBytes == buffers[0]->SizeInBytes()) {
        buffers[0]->GetDevice()->log(LogLevel::SL_LOG_LEVEL_WARN) << "offsetIndex * offsetSize == SizeInBytes(), wrapping around" << std::endl;
        offsetIndex = 0;
    }
}

void FrameIndexedBuffer::ResetOffsetIndex(void)
{
    offsetIndex = -1;
}

uint64_t FrameIndexedBuffer::CurrentOffsetInBytes(void) const
{
    return (offsetSizeInBytes == 0) ? 0 : offsetIndex * offsetSizeInBytes;
}

uint64_t FrameIndexedBuffer::CurrentOffset(void) const
{
    return (offsetSizeInBytes == 0) ? 0 : offsetIndex;
}

SL_VECTOR(Buffer*)& FrameIndexedBuffer::GetBuffers(void)
{
    return buffers;
}
}
}
