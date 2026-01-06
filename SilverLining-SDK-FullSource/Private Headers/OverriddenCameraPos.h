// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

/** \file OverriddenCameraPos.h
*/

#pragma once

#include "MemAlloc.h"
#include "Vector3.h"

namespace SilverLining
{
    class Camera;
    class OverriddenCameraPos
    {
    public:
        OverriddenCameraPos(const Camera* camera);
        OverriddenCameraPos(const Vector3& overridePos);
        Vector3 GetCamPos() const;
    private:
        Vector3 camPos;
        Vector3 camPosOriginal;
        bool isSet;
    };
}
