#pragma once

#include "SampleDefines.h"

namespace SilverLining
{
    class Atmosphere;
}

class LayerManager;

class SampleState
{
public:
    SampleState(LayerManager& _layerManger);
    void DoAction(Action action);
    float yaw, pitch, roll, translation;
private:
    LayerManager& layerManger;
};
