#include "SilverLiningGroup.h"

#include <SilverLining.h>
#include <VulkanInitInfo.h>

#include <vsg/vk/State.h>

#define SILVERLINING_LICENSE_USER "YOUR USER NAME"
#define SILVERLINING_LICENSE_CODE "YOUR LICENSE CODE"

SilverLiningGroup::SilverLiningGroup(vsg::ref_ptr<vsg::Window> window)
    : _window(window)
{
    _atmosphere = new SilverLining::Atmosphere(SILVERLINING_LICENSE_USER, SILVERLINING_LICENSE_CODE);

    auto vsgDevice = _window->getOrCreateDevice();

    VkDevice device = *vsgDevice;
    VkPhysicalDevice physicalDevice = *(vsgDevice->getPhysicalDevice());
    VkInstance instance = *(vsgDevice->getInstance());

    uint32_t graphicsQueueFamilyIndex = 0;
    std::tie(graphicsQueueFamilyIndex, std::ignore) = vsgDevice->getPhysicalDevice()->getQueueFamily(_window->traits()->queueFlags, _window->getSurface());
    auto queue = vsgDevice->getQueue(graphicsQueueFamilyIndex);
    VkQueue graphicsQueue = *queue;

    VkRenderPass renderPass = *(_window->getOrCreateRenderPass());
    VkSampleCountFlagBits sampleCount = (VkSampleCountFlagBits)(_window->traits()->samples);
    VkFormat colorFormat = _window->traits()->swapchainPreferences.surfaceFormat.format;
    VkFormat depthFormat = _window->traits()->depthFormat;

    SilverLining::Vulkan::VulkanInitInfo info{ instance, physicalDevice, device
            , graphicsQueue, graphicsQueueFamilyIndex
            , renderPass, sampleCount, colorFormat, depthFormat
            , 1.0f, 0.0f, _window->traits()->swapchainPreferences.imageCount, nullptr}; // note, reversed because vsg uses rev z by default

    std::cout << "SilverLining atmosphere initializing..." << std::endl;

#ifdef WIN32
    // so that the depth range can be set to rev z as required for vsg
    int err = _atmosphere->Initialize(SilverLining::Atmosphere::VULKAN, "..\\..\\Resources", true, &info);
    //int err = _atmosphere->Initialize(SilverLining::Atmosphere::VULKAN, "D:\\poojan\\projects\\sundog\\silverlining2\\Resources", true, &info);
#else
    err = _atmosphere->Initialize(SilverLining::Atmosphere::VULKAN, "../../../Resources", true, &info);
#endif
    if (err != SilverLining::Atmosphere::E_NOERROR) {
        throw std::runtime_error("could not initialize silverlining!!!");
    }

    std::cout << "SilverLining atmosphere initialized" << std::endl;

    _atmosphere->SetConfigOption("billboard-spin-enabled-override-disabled", "yes");

    _atmosphere->SetUpVector(0, 0, 1);
    _atmosphere->SetRightVector(1, 0, 0);
    _atmosphere->SetViewport(0, 0, _window->traits()->width, _window->traits()->height);

    SetTimeAndLocation();
}

SilverLiningGroup::~SilverLiningGroup()
{
    delete _atmosphere;
}

void SilverLiningGroup::SetTimeAndLocation()
{
    SilverLining::Location loc;
    loc.SetLatitude(45);
    loc.SetLongitude(-122);

    SilverLining::LocalTime tm;
    tm.SetYear(1971);
    tm.SetMonth(8);
    tm.SetDay(5);
    tm.SetHour(6);
    tm.SetMinutes(30);
    tm.SetSeconds(0);
    tm.SetObservingDaylightSavingsTime(true);
    tm.SetTimeZone(SilverLining::PST);

    _atmosphere->GetConditions()->SetTime(tm);
    _atmosphere->GetConditions()->SetLocation(loc);
}

void SilverLiningGroup::AdvanceTime(long delta)
{
    SilverLining::LocalTime lt = _atmosphere->GetConditions()->GetTime();
    lt.AddSeconds(delta);
    _atmosphere->GetConditions()->SetTime(lt);
    std::cout << "Time is: " << lt << std::endl;
}

void SilverLiningGroup::setUpSlViewProjection(vsg::RecordTraversal& rt) const
{
    const auto& projectionMatrix = rt.getState()->projectionMatrixStack.top();
    const auto& viewMatrix       = rt.getState()->modelviewMatrixStack.top();


    double mv[16], proj[16];

    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            mv[4 * i + j] = viewMatrix[i][j];
            proj[4 * i + j] = projectionMatrix[i][j];
        }

    _atmosphere->SetCameraMatrix(mv);
    _atmosphere->SetProjectionMatrix(proj);
}

void SilverLiningGroup::accept(vsg::RecordTraversal& rt) const
{
    setUpSlViewProjection(rt);
    // traverse children
    traverse(rt);
}

SilverLining::Atmosphere* SilverLiningGroup::atmosphere(void)
{
    return _atmosphere;
}

int SilverLiningGroup::imageIndex(void) const
{
    return _window->imageIndex();
}

