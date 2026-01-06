#include "stdafx.h"
#include "LayerManager.h"

#include "SilverLining.h"
#include <string>

using namespace SilverLining;

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

std::string LayerManager::MakeStringEnabledLayers(void) const
{
    std::string enabledLayersString;
    for (size_t i = 0; i < layers.size(); ++i) {
        if (layers[i]->GetEnabled()) {
            if (enabledLayersString.empty() == false) {
                enabledLayersString += "And";
            }
            enabledLayersString += std::string(layers[i]->GetName());
        }
    }
    return enabledLayersString;
}

void LayerManager::AddLayer(CloudLayer* layer)
{
    layers.push_back(layer);
    atmosphere.GetConditions()->AddCloudLayer(layer);
    layer->SetName(typeToString(layer->GetType()).c_str());
    layer->SetEnabled(false);
}

Atmosphere* LayerManager::GetAtmosphere(void) const
{
    return &atmosphere;
}
// Configure high cirrus clouds.
static void SetupCirrusClouds(LayerManager& layerManager, const Atmosphere& atmosphere)
{
    CloudLayer *cirrusCloudLayer;

    cirrusCloudLayer = CloudLayerFactory::Create(CIRRUS_FIBRATUS, atmosphere);
    cirrusCloudLayer->SetBaseAltitude(6000);
    cirrusCloudLayer->SetThickness(0);
    cirrusCloudLayer->SetBaseLength(100000);
    cirrusCloudLayer->SetBaseWidth(100000);
    cirrusCloudLayer->SetLayerPosition(0, 0);
    cirrusCloudLayer->SeedClouds(atmosphere);

    layerManager.AddLayer(cirrusCloudLayer);
}

// Add a cumulus congestus deck with 80% sky coverage.
static void SetupCumulusCongestusClouds(LayerManager& layerManager, const Atmosphere& atmosphere)
{
    CloudLayer *cumulusCongestusLayer;

    cumulusCongestusLayer = CloudLayerFactory::Create(CUMULUS_CONGESTUS, atmosphere);
    cumulusCongestusLayer->SetIsInfinite(true);
    cumulusCongestusLayer->SetBaseAltitude(1500);
    cumulusCongestusLayer->SetThickness(100);
    cumulusCongestusLayer->SetBaseLength(30000);
    cumulusCongestusLayer->SetBaseWidth(30000);
    cumulusCongestusLayer->SetDensity(0.8);
    cumulusCongestusLayer->SetLayerPosition(0, 0);
    cumulusCongestusLayer->SetCloudAnimationEffects(0.2, false);
    cumulusCongestusLayer->SeedClouds(*layerManager.GetAtmosphere());
    cumulusCongestusLayer->SetAlpha(0.7);
    cumulusCongestusLayer->SetFadeTowardEdges(true);

    layerManager.AddLayer(cumulusCongestusLayer);
}


// Sets up a solid stratus deck.
void SetupStratusClouds(LayerManager& layerManager, const Atmosphere& atmosphere)
{
    CloudLayer *stratusLayer;

    stratusLayer = CloudLayerFactory::Create(STRATUS, atmosphere);
    stratusLayer->SetIsInfinite(true);
    stratusLayer->SetBaseAltitude(1000);
    stratusLayer->SetThickness(600);
    stratusLayer->SetDensity(1.0);
    stratusLayer->SetLayerPosition(0, 0);
    stratusLayer->SeedClouds(*layerManager.GetAtmosphere());

    layerManager.AddLayer(stratusLayer);
}

// A thunderhead; note a Cumulonimbus cloud layer contains a single cloud.
static void SetupCumulonimbusClouds(LayerManager& layerManager, const Atmosphere& atmosphere)
{
    CloudLayer *cumulonimbusLayer;

    cumulonimbusLayer = CloudLayerFactory::Create(CUMULONIMBUS_CAPPILATUS, atmosphere);
    cumulonimbusLayer->SetBaseAltitude(1000);
    cumulonimbusLayer->SetThickness(3000);
    cumulonimbusLayer->SetBaseLength(3000);
    cumulonimbusLayer->SetBaseWidth(5000);
    cumulonimbusLayer->SetLayerPosition(5000, 5000);
    cumulonimbusLayer->SeedClouds(*layerManager.GetAtmosphere());

    layerManager.AddLayer(cumulonimbusLayer);
}

// Cumulus mediocris are little, puffy clouds. Keep the density low for realism, otherwise
// you'll have a LOT of clouds because they are small.
static void SetupCumulusMediocrisClouds(LayerManager& layerManager, const Atmosphere& atmosphere)
{
    CloudLayer *cumulusMediocrisLayer;

    cumulusMediocrisLayer = CloudLayerFactory::Create(CUMULUS_MEDIOCRIS, atmosphere);
    cumulusMediocrisLayer->SetIsInfinite(true);
    cumulusMediocrisLayer->SetBaseAltitude(1000);
    cumulusMediocrisLayer->SetThickness(100);
    cumulusMediocrisLayer->SetBaseLength(20000);
    cumulusMediocrisLayer->SetBaseWidth(20000);
    cumulusMediocrisLayer->SetDensity(0.2);
    cumulusMediocrisLayer->SetLayerPosition(0, 0);
    cumulusMediocrisLayer->SeedClouds(*layerManager.GetAtmosphere());

    layerManager.AddLayer(cumulusMediocrisLayer);
}

// Stratocumulus clouds are rendered with GPU ray-casting. On systems that can support it
// (Shader model 3.0+) this enables very dense cloud layers with per-fragment lighting.
static void SetupStratocumulusClouds(LayerManager& layerManager, const Atmosphere& atmosphere)
{
    CloudLayer *stratocumulusLayer;

    stratocumulusLayer = CloudLayerFactory::Create(STRATOCUMULUS, atmosphere);
    stratocumulusLayer->SetBaseAltitude(1000);
    stratocumulusLayer->SetThickness(3000);
    stratocumulusLayer->SetBaseLength(30000);
    stratocumulusLayer->SetBaseWidth(30000);
    stratocumulusLayer->SetDensity(1.0);
    stratocumulusLayer->SetIsInfinite(true);
    stratocumulusLayer->SetAlpha(1.0);
    stratocumulusLayer->SetFadeTowardEdges(true);
    stratocumulusLayer->SetLayerPosition(0, 0);
    stratocumulusLayer->SeedClouds(*layerManager.GetAtmosphere());

    layerManager.AddLayer(stratocumulusLayer);
}


static void SetupStratocumulusParticles(LayerManager& layerManager, const Atmosphere& atmosphere)
{
    CloudLayer* stratocumulusParticlesLayer = CloudLayerFactory::Create(STRATOCUMULUS_PARTICLES, atmosphere);
    stratocumulusParticlesLayer->SetBaseAltitude(1000);
    stratocumulusParticlesLayer->SetThickness(3000);
    stratocumulusParticlesLayer->SetBaseLength(30000);
    stratocumulusParticlesLayer->SetBaseWidth(30000);
    stratocumulusParticlesLayer->SetDensity(0.5);
    stratocumulusParticlesLayer->SetIsInfinite(true);
    stratocumulusParticlesLayer->SetAlpha(1.0);
    stratocumulusParticlesLayer->SetFadeTowardEdges(true);
    stratocumulusParticlesLayer->SetLayerPosition(0, 0);
    stratocumulusParticlesLayer->SeedClouds(*layerManager.GetAtmosphere());

    layerManager.AddLayer(stratocumulusParticlesLayer);
}

// Sandstorms should be positioned at ground level. There is no need to set their
// density or thickness.
static void SetupSandstorm(LayerManager& layerManager, const Atmosphere& atmosphere)
{
    CloudLayer *sandstormLayer;

    sandstormLayer = CloudLayerFactory::Create(SANDSTORM, atmosphere);
    sandstormLayer->SetIsInfinite(false);
    sandstormLayer->SetLayerPosition(0, -25000);
    sandstormLayer->SetBaseAltitude(0);
    sandstormLayer->SetBaseLength(50000);
    sandstormLayer->SetBaseWidth(50000);
    sandstormLayer->SeedClouds(*layerManager.GetAtmosphere());

    layerManager.AddLayer(sandstormLayer);
}


LayerManager::LayerManager(SilverLining::Atmosphere* _atmosphere)
    : atmosphere(*_atmosphere)
{
    // Set up the desired cloud types.
    SetupCirrusClouds(*this, atmosphere);
    SetupCumulusCongestusClouds(*this, atmosphere);
    SetupStratusClouds(*this, atmosphere);
    SetupCumulonimbusClouds(*this, atmosphere);
    SetupCumulusMediocrisClouds(*this, atmosphere);
    SetupStratocumulusClouds(*this, atmosphere);
    SetupStratocumulusParticles(*this, atmosphere);
    //SetupSandstorm();

    NextCloudLayer();
}

void LayerManager::NextCloudLayer()
{
    static bool firstTime = true;

    if (firstTime) {
        if (layers.size() > 0) {
            layers[0]->SetEnabled(true);
        }
        if (layers.size() > 1) {
            layers[1]->SetEnabled(true);
        }
        firstTime = false;
        return;
    }

    atmosphere.GetConditions()->SetVisibility(atmosphere.GetConditions()->GetVisibility() * 10);

    static int currLayer = -1;

    if (currLayer == -1) {
        for (size_t i = 0; i < layers.size(); ++i) {
            layers[i]->SetEnabled(false);
        }
        if (layers.size()>0) {
            currLayer = 0;
        }
    } else {
        layers[currLayer]->SetEnabled(false);
        currLayer = (currLayer + 1) % layers.size();
    }
    if (currLayer != -1) {
        layers[currLayer]->SetEnabled(true);
    }
}

void LayerManager::NextPrecipitationType()
{
    static CloudLayer::PrecipitationTypes precipitationType = CloudLayer::NONE;
    precipitationType = (CloudLayer::PrecipitationTypes)((precipitationType + 1) % CloudLayer::NUM_PRECIP_TYPES);
    atmosphere.GetConditions()->SetPrecipitation(precipitationType, 15.0);
}