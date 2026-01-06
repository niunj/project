// Copyright (c) 2004-2014  Sundog Software, LLC. All rights reserved worldwide.

#include "CloudDistributorRandom.h"
#include "Cloud.h"
#include "Renderer.h"
#include "Billboard.h"
#include "RandomNumberGenerator.h"
#include "MathUtils.h"
#include "Camera.h"
#include "SLAssert.h"
#include <math.h>
#include <stdlib.h>

using namespace SilverLining;
using namespace std;

CloudDistributorRandom::CloudDistributorRandom() : minCorner(0,0,0), width(0), depth(0),
    height(0)
{
}

bool CloudDistributorRandom::Init(const Vector3& pMinCorner, const Vector3& pMaxCorner, double pVoxelSize)
{
    layerCenterX = (pMaxCorner.x - pMinCorner.x) * 0.5 + pMinCorner.x;
    layerCenterZ = (pMaxCorner.z - pMinCorner.z) * 0.5 + pMinCorner.z;

    width = (int)((pMaxCorner.x - pMinCorner.x) / pVoxelSize);
    depth = (int)((pMaxCorner.z - pMinCorner.z) / pVoxelSize);
    height = pMaxCorner.y - pMinCorner.y;
    voxelSize = pVoxelSize;

    minCorner = pMinCorner;

    maxDiameter = width < depth ? width : depth;

    int centerX = width >> 1;
    int centerY = depth >> 1;

    for (int diameter = 1; diameter < maxDiameter; diameter++) {
        int radius = diameter >> 1;
        int widthSlop = centerX - radius;
        int depthSlop = centerY - radius;

        SL_VECTOR(Vector2i)& vec = slots[diameter];
        vec.reserve((widthSlop + widthSlop) * (depthSlop + depthSlop));

        int minX = centerX - widthSlop;
        int maxX = centerX + widthSlop;
        int minY = centerY - depthSlop;
        int maxY = centerY + depthSlop;

        SL_ASSERT(minX >= 0);
        SL_ASSERT(minY >= 0);
        SL_ASSERT(minX <= maxDiameter);
        SL_ASSERT(maxY <= maxDiameter);

        for (int x = minX; x < maxX; x++) {
            for (int y = minY; y < maxY; y++) {
                vec.push_back(Vector2i(x, y));
            }
        }
    }

    return true;
}

CloudDistributorRandom::~CloudDistributorRandom()
{
}

bool CloudDistributorRandom::PlaceCloud(Cloud *cloud, double minimumDistanceScale, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData)
{
    double cloudWidth, cloudDepth, cloudHeight;
    cloud->GetSize(cloudWidth, cloudDepth, cloudHeight);

    double cloudDiameter = cloudWidth > cloudDepth ? cloudWidth : cloudDepth;

    int cloudDiameterVoxels = (int)(cloudDiameter / voxelSize);

    int nSlots = (int)slots[cloudDiameterVoxels].size();

    if (nSlots > 0) {
        int slot = rng->UniformRandomIntRange(0, nSlots - 1);

        Occupy(slots[cloudDiameterVoxels][slot].x, slots[cloudDiameterVoxels][slot].y,
               cloudDiameterVoxels);

        double x = minCorner.x + slots[cloudDiameterVoxels][slot].x * voxelSize;
        double z = minCorner.z + slots[cloudDiameterVoxels][slot].y * voxelSize;

        double heightSlop = height - cloudHeight;
        if (heightSlop < 0) heightSlop = 0;

        double rndy = rng->UniformRandomDouble();
        double heightOffset = rndy * heightSlop;
        double y = minCorner.y + heightOffset;

        const Camera* camera = tcsData->GetCamera();
        cloud->SetWorldPosition(Vector3(x, y, z) * camera->GetBasis3x3());

        // calc position relative to center of deck
        Vector3 layerPos;
        layerPos.x = x - layerCenterX;
        layerPos.y = y - minCorner.y;
        layerPos.z = z - layerCenterZ;
        cloud->SetLayerPosition(layerPos);

        return true;
    }

    return false;
}

void CloudDistributorRandom::Occupy(int x, int y, int diameter)
{
    Vector2i pos(x, y);
    int radius = diameter >> 1;

    for (int d = 1; d < maxDiameter; d++) {
        int r = d >> 1;

        SL_VECTOR(Vector2i)& vec = slots[d];

        SL_VECTOR(Vector2i) ::reverse_iterator it, rbegin, rend;
        SL_VECTOR(Vector2i) ::iterator begin;
        SL_VECTOR(int) eraseList;
        SL_VECTOR(int)::iterator eraseListIt;

        int idx = (int)vec.size() - 1;
        rbegin = vec.rbegin();
        rend = vec.rend();
        begin = vec.begin();

        for (it = rbegin; it != rend; it++) {
            Vector2i p = *it;
            if ((MathUtils::Abs(pos.x - p.x) < radius + r) &&
                    (MathUtils::Abs(pos.y - p.y) < radius + r)) {
                double dx = pos.x - p.x;
                double dy = pos.y - p.y;
                double dist2 = dx * dx + dy * dy;

                double minDist = r + radius;

                if (dist2 < minDist) {
                    eraseList.push_back(idx);
                }

                idx--;
            }
        }

        for (eraseListIt = eraseList.begin(); eraseListIt != eraseList.end(); eraseListIt++) {
            vec.erase(begin + *eraseListIt);
        }

    }
}
