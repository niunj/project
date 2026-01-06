#include "CloudLayerSerializationUtils.h"
#include "CloudLayerTcsUserData.h"
#include "CloudLayer.h"

#define CLOUDLAYER_SERIALIZE_VERSION 15

namespace SilverLining
{

void CloudLayerSerializationUtils::Serialize(std::ostream& s, const CloudLayer& cloudLayer, ThreadCameraStreamData* tcsData)
{
    int version = CLOUDLAYER_SERIALIZE_VERSION;
    s.write((char *)&version, sizeof(int));

    const CloudLayerTcsUserData* tcsUserData = cloudLayer.GetOrCreateCloudLayerTcsUserData(tcsData);
    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;
    const double baseAltitude = tcsUserData->baseAltitude;
    const bool renderingEnabled = tcsUserData->renderingEnabled;
    const double coverageMultiplier = tcsUserData->coverageMultiplier;

    s.write((char *)&layerX, sizeof(double));
    s.write((char *)&layerZ, sizeof(double));
    s.write((char *)&cloudLayer.baseWidth, sizeof(double));
    s.write((char *)&cloudLayer.baseLength, sizeof(double));
    s.write((char *)&baseAltitude, sizeof(double));
    s.write((char *)&cloudLayer.thickness, sizeof(double));
    s.write((char *)&cloudLayer.density, sizeof(double));
    s.write((char *)&cloudLayer.layerEnabled, sizeof(bool));
    s.write((char *)&renderingEnabled, sizeof(bool));
    s.write((char *)&cloudLayer.isInfinite, sizeof(bool));

    s.write((char *)&cloudLayer.localWindX, sizeof(double));
    s.write((char *)&cloudLayer.localWindZ, sizeof(double));

    s.write((char *)&cloudLayer.cloudWrap, sizeof(bool));
    s.write((char *)&cloudLayer.fadeTowardEdges, sizeof(bool));

    s.write((char *)&coverageMultiplier, sizeof(double));

    int nPrecip = (int)cloudLayer.precipitationEffects.size();
    s.write((char *)&nPrecip, sizeof(int));
    SL_MAP(int, double) ::const_iterator it;
    for (it = cloudLayer.precipitationEffects.begin(); it != cloudLayer.precipitationEffects.end(); it++) {
        int type = it->first;
        double intensity = it->second;
        s.write((char *)&type, sizeof(int));
        s.write((char *)&intensity, sizeof(double));
    }

    s.write((char *)&cloudLayer.curveTowardGround, sizeof(bool));
    s.write((char *)&cloudLayer.alpha, sizeof(double));

    s.write((char *)&cloudLayer.usingCloudAtlas, sizeof(bool));
}

bool CloudLayerSerializationUtils::UnSerialize(std::istream& s, CloudLayer& cloudLayer, ThreadCameraStreamData* tcsData)
{
    int version;
    s.read((char *)&version, sizeof(int));
    if (version != CLOUDLAYER_SERIALIZE_VERSION) return false;

    CloudLayerTcsUserData* tcsUserData = cloudLayer.GetOrCreateCloudLayerTcsUserData(tcsData);
    double& layerX = tcsUserData->layerX;
    double& layerZ = tcsUserData->layerZ;
    double& baseAltitude = tcsUserData->baseAltitude;
    bool& renderingEnabled = tcsUserData->renderingEnabled;

    s.read((char *)&layerX, sizeof(double));
    s.read((char *)&layerZ, sizeof(double));
    s.read((char *)&cloudLayer.baseWidth, sizeof(double));
    s.read((char *)&cloudLayer.baseLength, sizeof(double));
    s.read((char *)&baseAltitude, sizeof(double));
    s.read((char *)&cloudLayer.thickness, sizeof(double));
    s.read((char *)&cloudLayer.density, sizeof(double));
    s.read((char *)&cloudLayer.layerEnabled, sizeof(bool));
    s.read((char *)&renderingEnabled, sizeof(bool));
    s.read((char *)&cloudLayer.isInfinite, sizeof(bool));

    s.read((char *)&cloudLayer.localWindX, sizeof(double));
    s.read((char *)&cloudLayer.localWindZ, sizeof(double));

    s.read((char *)&cloudLayer.cloudWrap, sizeof(bool));
    s.read((char *)&cloudLayer.fadeTowardEdges, sizeof(bool));

    s.read((char *)&tcsUserData->coverageMultiplier, sizeof(double));

    cloudLayer.precipitationEffects.clear();
    int nPrecip = 0;
    s.read((char *)&nPrecip, sizeof(int));
    for (int i = 0; i < nPrecip; i++) {
        int precipType;
        double intensity;
        s.read((char *)&precipType, sizeof(int));
        s.read((char *)&intensity, sizeof(double));
        cloudLayer.precipitationEffects[precipType] = intensity;
    }

    s.read((char *)&cloudLayer.curveTowardGround, sizeof(bool));
    s.read((char *)&cloudLayer.alpha, sizeof(double));

    s.read((char *)&cloudLayer.usingCloudAtlas, sizeof(bool));

    return true;
}
}