#include "SilverLiningCommand.h"
#include "SilverLiningGroup.h"

#include "SetUpLayers.h"

#include <vsg/vk/CommandBuffer.h>

#include <SilverLining.h>

#include <iostream>

SilverLiningCommand::SilverLiningCommand(SilverLining::Atmosphere* atmosphere, SilverLiningGroup* parent) :
    _atmosphere(atmosphere)
    , _parent(parent)
{
    _atmosphere->GetRandomNumberGenerator()->Reset();

    _layersContainer = new LayersContainer();

}

SilverLiningCommand::~SilverLiningCommand()
{
    delete _layersContainer;
}

static bool singleLayerOnly = false;

void SilverLiningCommand::NextCloudLayer()
{
    static bool firstTime = true;

    const std::vector<SilverLining::CloudLayer*>& layers = _layersContainer->GetLayers();

    if (firstTime && !singleLayerOnly) {

        if (layers.size() > 0) {
            layers[0]->SetEnabled(true, 0, _atmosphere->GetDefaultTcsData());
        }
        if (layers.size() > 1) {
            layers[1]->SetEnabled(true, 0, _atmosphere->GetDefaultTcsData());
        }

        firstTime = false;
        return;
    }

//    _atmosphere->GetConditions()->SetVisibility(_myKVisibility * 10);

    static int currLayer = -1;

    if (currLayer == -1) {
        for (size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
            layers[layerIndex]->SetEnabled(false, 0, _atmosphere->GetDefaultTcsData());
        }
        if (layers.size() > 0) {
            currLayer = 0;
        }
    } else {
        layers[currLayer]->SetEnabled(false, 0, _atmosphere->GetDefaultTcsData());
        currLayer = (currLayer + 1) % layers.size();
    }

    if (currLayer != -1) {
        layers[currLayer]->SetEnabled(true, 0, _atmosphere->GetDefaultTcsData());
    }
}

void SilverLiningCommand::compile(vsg::Context& context)
{
    if (_compiled) {
        return;
    }

    // Set up the desired cloud types.
    //_layersContainer->SetupCirrusFibratusClouds(_atmosphere, true, _atmosphere->GetDefaultTcsData());   // 5:50 time
    _layersContainer->SetupCumulusCongestusClouds(_atmosphere, true, _atmosphere->GetDefaultTcsData()); // 6:30 time
    //_layersContainer->SetupStratusClouds(_atmosphere, true, _atmosphere->GetDefaultTcsData()); // 6:30 time
    //_layersContainer->SetupCumulonimbusClouds(_atmosphere, true, _atmosphere->GetDefaultTcsData());
    //_layersContainer->SetupCumulusMediocrisClouds(_atmosphere, true, _atmosphere->GetDefaultTcsData());
    //_layersContainer->SetupStratocumulusClouds(_atmosphere, true, _atmosphere->GetDefaultTcsData());
    //_layersContainer->SetupStratocumulusParticles(_atmosphere, true, _atmosphere->GetDefaultTcsData());
    _layersContainer->SetupCirroCumulusClouds(_atmosphere, true, _atmosphere->GetDefaultTcsData());
    //_layersContainer->SetupSandstorm(_atmosphere, true, _atmosphere->GetDefaultTcsData());

    NextCloudLayer();
    _compiled = true;
}
void SilverLiningCommand::record(vsg::CommandBuffer& cb) const
{
    VkCommandBuffer commandBuffer = cb.vk();

    _atmosphere->GetDefaultTcsData()->SetStream(commandBuffer, (int)_parent->imageIndex());

    int viewportsl[4];
    _atmosphere->GetDefaultTcsData()->GetCamera()->GetViewport(viewportsl);

    int width = viewportsl[2];
    int height = viewportsl[3];

    VkViewport viewport{};
    viewport.width = (float)width;
    if (false) {
        // https://www.saschawillems.de/blog/2019/03/29/flipping-the-vulkan-viewport/
        viewport.height = (float) - height;
        viewport.y = (float)height;
    } else {
        viewport.height = (float)height;
    }
    viewport.minDepth = 0;
    viewport.maxDepth = 1;

    VkRect2D scissor{};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = viewportsl[2];
    scissor.extent.height = viewportsl[3];

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const bool geocentricMode = false;

    bool drawSky = true;
    double skyBoxDimension = 0;
    bool drawStars = false;
    bool clearDepth = false;
    bool drawSunAndMoon = true;

    _atmosphere->DrawSky(drawSky, geocentricMode, skyBoxDimension
                         , drawStars, clearDepth, drawSunAndMoon, 0, 0, 0, 0);

    const bool drawClouds = true;
    const bool drawPrecipitation = false;

    const bool enableDepthTest = true;
    const bool enableDepthWrites = false;

    SilverLining::CameraHandle cameraHandle = 0;
    const bool backFaceCullClockWise = true;
    const bool drawBackdrops = false;

    const bool drawLightning = false;

    float crepuscular = 0.8f;

    _atmosphere->DrawObjects(drawClouds, drawPrecipitation, enableDepthTest, 0, enableDepthWrites, cameraHandle, backFaceCullClockWise, drawBackdrops, drawLightning, geocentricMode);


}