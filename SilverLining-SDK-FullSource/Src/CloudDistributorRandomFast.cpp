// Copyright (c) 2004-2013  Sundog Software, LLC. All rights reserved worldwide.

#include "CloudDistributorRandomFast.h"
#include "Cloud.h"
#include "CloudLayer.h"
#include "Camera.h"
#include "Configuration.h"
#include "Billboard.h"
#include "SLAssert.h"
#include <stdlib.h>

using namespace SilverLining;

CloudDistributorRandomFast::CloudDistributorRandomFast() : minCorner(0,0,0), maxCorner(0,0,0)
{
}

bool CloudDistributorRandomFast::Init(const Vector3& pMinCorner, const Vector3& pMaxCorner, double pVoxelSize)
{
    minCorner = pMinCorner;
    maxCorner = pMaxCorner;

    layerCenterX = (maxCorner.x - minCorner.x) * 0.5 + minCorner.x;
    layerCenterZ = (maxCorner.z - minCorner.z) * 0.5 + minCorner.z;

    cloudPositionList.clear();

    return true;
}

CloudDistributorRandomFast::~CloudDistributorRandomFast()
{
}

bool CloudDistributorRandomFast::PlaceCloud(Cloud *cloud, double minimumDistanceScale, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData)
{
    double cloudWidth, cloudDepth, cloudHeight;
    cloud->GetSize(cloudWidth, cloudDepth, cloudHeight);

    double minX = minCorner.x + cloudWidth * 0.5;
    double maxX = maxCorner.x - cloudWidth * 0.5;
    double minY = minCorner.y;
    double maxY = maxCorner.y;
    double minZ = minCorner.z + cloudDepth * 0.5;
    double maxZ = maxCorner.z - cloudDepth * 0.5;

    Vector3 cloudPos;

    int maxRetries = 20;
    Configuration::GetIntValue("max-cloud-position-retries", maxRetries);

    bool neverIntersect = false;
    Configuration::GetBoolValue("cloud-never-allow-intersection", neverIntersect);

    CloudPlacement placement;
    placement.radius = cloudWidth > cloudDepth ? cloudWidth * 0.5 : cloudDepth * 0.5;
    placement.radius *= minimumDistanceScale;

    int i;
    bool success = false;
    for (i = 0; i < maxRetries; i++) {
        double rndx = rng->UniformRandomDouble();
        double rndy = rng->UniformRandomDouble();
        double rndz = rng->UniformRandomDouble();

        cloudPos.x = minX + (maxX - minX) * rndx;
        cloudPos.y = minY + (maxY - minY) * rndy;
        cloudPos.z = minZ + (maxZ - minZ) * rndz;

        SL_VECTOR(CloudPlacement)::const_iterator it;
        bool tooClose = false;
        for (it = cloudPositionList.begin(); it != cloudPositionList.end(); it++) {
            double sqRadiusSum = (*it).radius + placement.radius;
            sqRadiusSum *= sqRadiusSum;
            if ( (cloudPos - (*it).cloudPos).SquaredLength() < sqRadiusSum) {
                tooClose = true;
                break;
            }
        }

        if (!tooClose) {
            success = true;
            break;
        }
    }

    if (!success && neverIntersect) {
        return false;
    }

    placement.cloudPos = cloudPos;
    cloudPositionList.push_back(placement);

    Vector3 layerPos;
    layerPos.x = cloudPos.x - layerCenterX;
    layerPos.y = cloudPos.y - minY;
    layerPos.z = cloudPos.z - layerCenterZ;

    cloud->SetNeedsGeocentricPlacement(true);
    const Camera* camera = tcsData->GetCamera();
    cloud->SetWorldPosition(cloudPos * camera->GetBasis3x3());
    cloud->SetLayerPosition(layerPos);

    return true;
}
