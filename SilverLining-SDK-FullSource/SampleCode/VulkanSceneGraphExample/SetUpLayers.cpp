// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.

#include "SetUpLayers.h"
#include <string>

#include "SilverLining.h"

static std::string typeToString(int type)
{
    if (type == CIRROCUMULUS) return "CirroCumulusCloudLayer";
    if (type == CUMULUS_MEDIOCRIS) return "CumulusMediocrisCloudLayer";
    if (type == CUMULUS_CONGESTUS) return "CumulusCongestusCloudLayer";
    if (type == CUMULUS_CONGESTUS_HI_RES) return "CumulusCongestusCloudLayer";
    if (type == SANDSTORM) return "SANDSTORM(CumulusCongestusCloudLayer)";
    if (type == CUMULONIMBUS_CAPPILATUS) return "CumulonimbusCloudLayer";
    if (type == CIRRUS_FIBRATUS) return "CirrusCloudLayer";
    if (type == STRATUS) return "StratusCloudLayer";
    if (type == STRATOCUMULUS_PARTICLES) return "StratocumulusParticleCloudLayer";
    if (type == STRATOCUMULUS) return "StratocumulusCloudLayer";
    if (type == TOWERING_CUMULUS) return "ToweringCumulusCloudLayer";
    return "UNKNOWN";
}

static void AddLayer(std::vector<SilverLining::CloudLayer*>& layers, SilverLining::Atmosphere* atmosphere, SilverLining::CloudLayer* layer)
{
    layers.push_back(layer);
    atmosphere->GetConditions()->AddCloudLayer(layer);
    layer->SetName(typeToString(layer->GetType()).c_str());
}

LayersContainer::LayersContainer()
    : myCirrusFibratusCloudsIndex(-1)
    , myCumulusCongestusCloudsIndex(-1)
    , myStratusCloudsIndex(-1)
    , myCumulonimbusCloudsIndex(-1)
    , myCumulusMediocrisCloudsIndex(-1)
    , myStratocumulusCloudsIndex(-1)
    , myStratocumulusParticlesIndex(-1)
    , myCirroCumulusCloudsIndex(-1)
    , mySandstormIndex(-1)
{

}

LayersContainer::~LayersContainer()
{
    // nothing to do
}


// Configure high cirrus clouds.
void LayersContainer::SetupCirrusFibratusClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData)
{
    SilverLining::CloudLayer* cirrusCloudLayer = (create) ? SilverLining::CloudLayerFactory::Create(CIRRUS_FIBRATUS, *atmosphere) : myLayers[myCirrusFibratusCloudsIndex];

    if (create) {
        cirrusCloudLayer->SetThickness(0);
        cirrusCloudLayer->SetBaseLength(100000);
        cirrusCloudLayer->SetBaseWidth(100000);
    }

    cirrusCloudLayer->SetBaseAltitude(6000, true, tcsData);
    cirrusCloudLayer->SetLayerPosition(0, 0, tcsData);
    cirrusCloudLayer->SeedClouds(*atmosphere, tcsData);
    cirrusCloudLayer->SetEnabled(false, 0, tcsData);

    if (create) {
        myCirrusFibratusCloudsIndex = (int)myLayers.size();
        AddLayer(myLayers, atmosphere, cirrusCloudLayer);
    }
}

// Add a cumulus congestus deck with 80% sky coverage.
void LayersContainer::SetupCumulusCongestusClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData)
{
    SilverLining::CloudLayer* cumulusCongestusLayer = (create) ? SilverLining::CloudLayerFactory::Create(CUMULUS_CONGESTUS_HI_RES, *atmosphere) : myLayers[myCumulusCongestusCloudsIndex];

    if (create) {
        cumulusCongestusLayer->SetIsInfinite(true);
        cumulusCongestusLayer->SetThickness(100);
        cumulusCongestusLayer->SetBaseLength(30000);
        cumulusCongestusLayer->SetBaseWidth(30000);
        cumulusCongestusLayer->SetDensity(0.8);
        cumulusCongestusLayer->SetAlpha(0.5);
        cumulusCongestusLayer->SetCloudAnimationEffects(0.2, false);
        cumulusCongestusLayer->SetFadeTowardEdges(true);
    }

    cumulusCongestusLayer->SetBaseAltitude(1500, true, tcsData);
    cumulusCongestusLayer->SetLayerPosition(0, 0, tcsData);
    cumulusCongestusLayer->SeedClouds(*atmosphere, tcsData);
    cumulusCongestusLayer->SetEnabled(false, 0, tcsData);

    if (create) {
        myCumulusCongestusCloudsIndex = (int)myLayers.size();
        AddLayer(myLayers, atmosphere, cumulusCongestusLayer);
    }
}

// Sets up a solid stratus deck.
void LayersContainer::SetupStratusClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData)
{
    SilverLining::CloudLayer* stratusLayer = (create) ? SilverLining::CloudLayerFactory::Create(STRATUS, *atmosphere) : myLayers[myStratusCloudsIndex];

    if (create) {
        stratusLayer->SetIsInfinite(true);
        stratusLayer->SetThickness(600);
        stratusLayer->SetDensity(1.0);
    }

    stratusLayer->SetBaseAltitude(1000, true, tcsData);
    stratusLayer->SetLayerPosition(0, 0, tcsData);
    stratusLayer->SeedClouds(*atmosphere, tcsData);
    stratusLayer->SetEnabled(false, 0, tcsData);

    if (create) {
        myStratusCloudsIndex = (int)myLayers.size();
        AddLayer(myLayers, atmosphere, stratusLayer);
    }
}

// A thunderhead; note a Cumulonimbus cloud layer contains a single cloud.
void LayersContainer::SetupCumulonimbusClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData)
{
    SilverLining::CloudLayer* cumulonimbusLayer = (create) ? SilverLining::CloudLayerFactory::Create(CUMULONIMBUS_CAPPILATUS, *atmosphere) : myLayers[myCumulonimbusCloudsIndex];

    if (create) {
        cumulonimbusLayer->SetThickness(3000);
        cumulonimbusLayer->SetBaseLength(3000);
        cumulonimbusLayer->SetBaseWidth(5000);
    }

    cumulonimbusLayer->SetBaseAltitude(1000, true, tcsData);
    cumulonimbusLayer->SetLayerPosition(0, -10000, tcsData);
    cumulonimbusLayer->SeedClouds(*atmosphere, tcsData);
    cumulonimbusLayer->SetEnabled(false, 0, tcsData);

    if (create) {
        myCumulonimbusCloudsIndex = (int)myLayers.size();
        AddLayer(myLayers, atmosphere, cumulonimbusLayer);
    }
}

// Cumulus mediocris are little, puffy clouds. Keep the density low for realism, otherwise
// you'll have a LOT of clouds because they are small.
void LayersContainer::SetupCumulusMediocrisClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData)
{
    SilverLining::CloudLayer* cumulusMediocrisLayer = (create) ? SilverLining::CloudLayerFactory::Create(CUMULUS_MEDIOCRIS, *atmosphere) : myLayers[myCumulusMediocrisCloudsIndex];

    if (create) {
        cumulusMediocrisLayer->SetIsInfinite(true);
        cumulusMediocrisLayer->SetThickness(100);
        cumulusMediocrisLayer->SetBaseLength(20000);
        cumulusMediocrisLayer->SetBaseWidth(20000);
        cumulusMediocrisLayer->SetDensity(0.2);
    }

    cumulusMediocrisLayer->SetBaseAltitude(1000, true, tcsData);
    cumulusMediocrisLayer->SetLayerPosition(0, 0, tcsData);
    cumulusMediocrisLayer->SeedClouds(*atmosphere, tcsData);
    cumulusMediocrisLayer->SetEnabled(false, 0, tcsData);

    if (create) {
        myCumulusMediocrisCloudsIndex = (int)myLayers.size();
        AddLayer(myLayers, atmosphere, cumulusMediocrisLayer);
    }
}

// Stratocumulus clouds are rendered with GPU ray-casting. On systems that can support it
// (Shader model 3.0+) this enables very dense cloud layers with per-fragment lighting.
void LayersContainer::SetupStratocumulusClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData)
{
    SilverLining::CloudLayer* stratocumulusLayer = (create) ? SilverLining::CloudLayerFactory::Create(STRATOCUMULUS, *atmosphere) : myLayers[myStratocumulusCloudsIndex];

    if (create) {
        stratocumulusLayer->SetBaseLength(30000);
        stratocumulusLayer->SetBaseWidth(30000);
        stratocumulusLayer->SetDensity(0.5);
        stratocumulusLayer->SetIsInfinite(true);
        stratocumulusLayer->SetAlpha(1.0);
        stratocumulusLayer->SetFadeTowardEdges(true);
    }

    // Set this again. It gets internally modified after seeding
    stratocumulusLayer->SetThickness(3000);
    stratocumulusLayer->SetBaseAltitude(1000, true, tcsData);
    stratocumulusLayer->SetLayerPosition(0, 0, tcsData);
    stratocumulusLayer->SeedClouds(*atmosphere, tcsData);
    stratocumulusLayer->SetEnabled(false, 0, tcsData);

    if (create) {
        myStratocumulusCloudsIndex = (int)myLayers.size();
        AddLayer(myLayers, atmosphere, stratocumulusLayer);
    }
}

void LayersContainer::SetupStratocumulusParticles(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData)
{
    SilverLining::CloudLayer* stratocumulusParticlesLayer = (create) ? SilverLining::CloudLayerFactory::Create(STRATOCUMULUS_PARTICLES, *atmosphere) : myLayers[myStratocumulusParticlesIndex];

    if (create) {
        stratocumulusParticlesLayer->SetThickness(3000);
        stratocumulusParticlesLayer->SetBaseLength(30000);
        stratocumulusParticlesLayer->SetBaseWidth(30000);
        stratocumulusParticlesLayer->SetDensity(0.5);
        stratocumulusParticlesLayer->SetIsInfinite(true);
        stratocumulusParticlesLayer->SetAlpha(1.0);
        stratocumulusParticlesLayer->SetFadeTowardEdges(true);
    }

    stratocumulusParticlesLayer->SetBaseAltitude(1000, true, tcsData);
    stratocumulusParticlesLayer->SetLayerPosition(0, 0, tcsData);
    stratocumulusParticlesLayer->SeedClouds(*atmosphere, tcsData);
    stratocumulusParticlesLayer->SetEnabled(false, 0, tcsData);

    if (create) {
        myStratocumulusParticlesIndex = (int)myLayers.size();
        AddLayer(myLayers, atmosphere, stratocumulusParticlesLayer);
    }
}


void LayersContainer::SetupCirroCumulusClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData)
{
    SilverLining::CloudLayer* cirroCumulusCloudsLayer = (create) ? SilverLining::CloudLayerFactory::Create(CIRROCUMULUS, *atmosphere) : myLayers[myCirroCumulusCloudsIndex];

    if (create) {
        cirroCumulusCloudsLayer->SetThickness(0);
        cirroCumulusCloudsLayer->SetBaseLength(100000);
        cirroCumulusCloudsLayer->SetBaseWidth(100000);
        cirroCumulusCloudsLayer->SetCloudAnimationEffects(0.1, true);
        cirroCumulusCloudsLayer->SetFadeTowardEdges(true);
    }

    cirroCumulusCloudsLayer->SetBaseAltitude(6000, true, tcsData);
    cirroCumulusCloudsLayer->SetLayerPosition(0, 0, tcsData);

    // hack for fading while seeding
    cirroCumulusCloudsLayer->SetIsInfinite(false);
    cirroCumulusCloudsLayer->SeedClouds(*atmosphere, tcsData);
    // hack for fading while seeding (undo)
    cirroCumulusCloudsLayer->SetIsInfinite(true);

    cirroCumulusCloudsLayer->SetEnabled(false, 0, tcsData);

    if (create) {
        myCirroCumulusCloudsIndex = (int)myLayers.size();
        AddLayer(myLayers, atmosphere, cirroCumulusCloudsLayer);
    }
}


// Sandstorms should be positioned at ground level. There is no need to set their
// density or thickness.
void LayersContainer::SetupSandstorm(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData)
{
    SilverLining::CloudLayer* sandstormLayer = (create) ? SilverLining::CloudLayerFactory::Create(SANDSTORM, *atmosphere) : myLayers[mySandstormIndex];

    if (create) {
        sandstormLayer->SetIsInfinite(false);
        sandstormLayer->SetBaseLength(10000);
        sandstormLayer->SetBaseWidth(10000);
    }

    sandstormLayer->SetLayerPosition(0, -25000, tcsData);
    sandstormLayer->SetBaseAltitude(0, true, tcsData);
    sandstormLayer->SeedClouds(*atmosphere, tcsData);
    sandstormLayer->SetEnabled(false, 0, tcsData);

    if (create) {
        mySandstormIndex = (int)myLayers.size();
        AddLayer(myLayers, atmosphere, sandstormLayer);
    }
}
void LayersContainer::EraseLayerEntry(int index)
{
    if (index >= (int)myLayers.size()) {
        return;
    }
    auto it = myLayers.begin();
    std::advance(it, index);
    myLayers.erase(it);
}



