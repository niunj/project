// Copyright (c) 2004-2018 Sundog Software, LLC All rights reserved worldwide.

#include "CloudLayerCompare.h"
#include "CloudLayer.h"
#include "Camera.h"
#include "MathUtils.h"

namespace SilverLining
{
CloudLayerCompare::CloudLayerCompare(const Camera* _camera, bool _enforceStrictWeak, ThreadCameraStreamData* _tcsData)
    : camera(_camera)
    , enforceStrictWeak(_enforceStrictWeak)
    , tcsData(_tcsData)
{

}

CloudLayerCompare::~CloudLayerCompare()
{

}

bool CloudLayerCompare::operator() (const CloudLayer* c1, const CloudLayer* c2)
{
    if (c1 == 0 || c2 == 0) return false;

    if (c1 == c2) return false;

    // Sort by altitude, then position

    // Do these layers overlap in altitude?
    double baseAlt1 = c1->GetBaseAltitudeGeocentric(tcsData);
    double baseAlt2 = c2->GetBaseAltitudeGeocentric(tcsData);

    double c1Top = baseAlt1 + c1->GetMaxHeight();
    double c2Top = baseAlt2 + c2->GetMaxHeight();
    double c1Bottom = baseAlt1;
    double c2Bottom = baseAlt2;

    bool c1TopInsideC2 = c1Top >= c2Bottom && c1Top <= c2Top;
    bool c1BottomInsideC2 = c1Bottom >= c2Bottom && c1Bottom <= c2Top;
    bool c2TopInsideC1 = c2Top >= c1Bottom && c2Top <= c1Top;
    bool c2BottomInsideC1 = c2Bottom >= c1Bottom && c2Bottom <= c1Top;

    bool overlap = c1TopInsideC2 || c1BottomInsideC2 || c2TopInsideC1 || c2BottomInsideC1;
    bool involvesInfinite = c1->GetIsInfinite() || c2->GetIsInfinite();

    if (!enforceStrictWeak && (!overlap || involvesInfinite)) {
        if (camera) {
            double camAlt = camera->GetAltitude();

            double distToBottom1 = MathUtils::Abs(camAlt - c1Bottom);
            double distToTop1 = MathUtils::Abs(camAlt - c1Top);

            double distToBottom2 = MathUtils::Abs(camAlt - c2Bottom);
            double distToTop2 = MathUtils::Abs(camAlt - c2Top);

            double d1 = distToBottom1 < distToTop1 ? distToBottom1 : distToTop1;
            double d2 = distToBottom2 < distToTop2 ? distToBottom2 : distToTop2;

            return (d1 > d2);
        } else {
            return false;
        }
    } else {

        double x, y, z;
        c1->GetSortPosition(x, y, z, camera, tcsData);
        Vector3 p1(x, y, z);

        c2->GetSortPosition(x, y, z, camera, tcsData);
        Vector3 p2(x, y, z);

        if (camera) {
            double d1 = (camera->GetPosition() - p1).SquaredLength();
            double d2 = (camera->GetPosition() - p2).SquaredLength();

            return (d1 > d2);
        } else {
            return false;
        }
    }
}
}