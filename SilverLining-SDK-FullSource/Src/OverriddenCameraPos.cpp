// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

#include "OverriddenCameraPos.h"
#include "Camera.h"
#include "SLAssert.h"

namespace SilverLining
{
OverriddenCameraPos::OverriddenCameraPos(const Camera* camera)
    : isSet(false)
{
    SL_ASSERT(camera);
    camPosOriginal = camera->GetPosition();

}
OverriddenCameraPos::OverriddenCameraPos(const Vector3& overridePos)
{
    camPos = overridePos;
    isSet = true;
}
Vector3 OverriddenCameraPos::GetCamPos() const
{
    if (isSet) {
        return camPos;
    } else {
        return camPosOriginal;
    }
}
}