// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

#include "CloudLayerFactory.h"
#include "CumulusMediocrisCloudLayer.h"
#include "CirrusCloudLayer.h"
#include "StratusCloudLayer.h"
#include "CumulusCongestusCloudLayer.h"
#include "CumulonimbusCloudLayer.h"
#include "CirroCumulusLayer.h"
#include "CirroStratusLayer.h"
#include "StratocumulusParticleCloudLayer.h"
#include "StratocumulusCloudLayer.h"
#include "ToweringCumulusCloudLayer.h"
#include "Configuration.h"
#include "Atmosphere.h"
#include "SLAssert.h"

namespace SilverLining
{
CloudLayer *CloudLayerFactory::Create(CloudTypes layerType, const Atmosphere& atmosphere)
{
    CloudLayer *cl = 0;

    if (Configuration::GetRefCount() <= 0) {
        return cl;
    }

    switch (layerType) {
    case CIRROCUMULUS:
        cl = SL_NEW CirroCumulusCloudLayer(atmosphere);
        break;
    case CIRROSTRATUS:
        cl = SL_NEW CirroStratusCloudLayer(atmosphere);
        break;
    case CUMULUS_MEDIOCRIS:
        cl = SL_NEW CumulusMediocrisCloudLayer(atmosphere);
        break;

    case CUMULUS_CONGESTUS:
        cl = SL_NEW CumulusCongestusCloudLayer(atmosphere);
        break;

    case CUMULUS_CONGESTUS_HI_RES: {
        cl = SL_NEW CumulusCongestusCloudLayer(atmosphere);
        cl->SetUsingCloudAtlas(true);
    }
    break;

    case SANDSTORM: {
        cl = SL_NEW CumulusCongestusCloudLayer(atmosphere);
        cl->SetIsSoft(true);
        cl->SetDensity(2.0);
        cl->SetThickness(0);
        cl->SetFadeTowardEdges(false);
        cl->SetBaseAltitude(0, true, atmosphere.GetDefaultTcsData());

        double r = 0.91, g = 0.72, b = 0.74;
        Configuration::GetDoubleValue("sandstorm-dust-red", r);
        Configuration::GetDoubleValue("sandstorm-dust-blue", b);
        Configuration::GetDoubleValue("sandstorm-dust-green", g);

        Vector3 dust(r, g, b);

        cl->OverrideCloudColor(dust);
    }
    break;

    case CUMULONIMBUS_CAPPILATUS:
        cl = SL_NEW CumulonimbusCloudLayer(atmosphere);
        break;

    case CIRRUS_FIBRATUS:
        cl = SL_NEW CirrusCloudLayer(atmosphere);
        break;

    case STRATUS:
        cl = SL_NEW StratusCloudLayer(atmosphere);
        break;

    case STRATOCUMULUS_PARTICLES:
        cl = SL_NEW StratocumulusParticleCloudLayer(atmosphere);
        break;

    case STRATOCUMULUS:
        cl = SL_NEW StratocumulusCloudLayer(atmosphere);
        break;

    case TOWERING_CUMULUS:
        cl = SL_NEW ToweringCumulusCloudLayer(atmosphere);
        break;

    case NUM_CLOUD_TYPES:
        break;
    }

    if (cl) {
        cl->SetType(layerType);
    }

    return cl;
}

bool CloudLayerFactory::Serialize(CloudLayer *layer, std::ostream& s, void* data)
{
    if (layer == 0) {
        return false;
    }
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : layer->GetAtmosphere().GetDefaultTcsData();
    SL_ASSERT(tcsData);

    int type = layer->GetType();
    s.write((char *)&type, sizeof(int));

    layer->Serialize(s, tcsData);

    return true;
}

CloudLayer *CloudLayerFactory::Unserialize(const Atmosphere& atmosphere, std::istream& s, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    int type;
    s.read((char *)&type, sizeof(int));

    CloudLayer* layer = Create((CloudTypes)type, atmosphere);

    SL_ASSERT(layer);
    if (layer == 0) {
        return 0;
    }

    if (layer) {
        layer->Unserialize(atmosphere, s, tcsData);
    }
    return layer;
}
}

