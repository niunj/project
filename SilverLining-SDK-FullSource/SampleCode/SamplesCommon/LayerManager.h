#pragma once

#include <vector>
#include <string>

namespace SilverLining
{
    class Atmosphere;
    class CloudLayer;
}

class LayerManager
{
public:
    LayerManager(SilverLining::Atmosphere* atmosphere);
    void NextCloudLayer();
    void NextPrecipitationType();
    void AddLayer(SilverLining::CloudLayer* layer);
    std::string MakeStringEnabledLayers(void) const;
    SilverLining::Atmosphere* GetAtmosphere(void) const;
    const SilverLining::Atmosphere& GetAtmosphereConstReference(void) const;
private:
    std::vector<SilverLining::CloudLayer*> layers;
    SilverLining::Atmosphere& atmosphere;
};