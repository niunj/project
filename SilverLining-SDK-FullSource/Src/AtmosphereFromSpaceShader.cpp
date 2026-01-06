// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.
#include "AtmosphereFromSpaceShader.h"

#include "ThreadCameraStreamData.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
AtmosphereFromSpaceShader::AtmosphereFromSpaceShader(ThreadCameraStreamData* _tcsData, bool doShadow)
    : tcsData(_tcsData)
{
    Renderer* renderer = tcsData->GetRenderer();

    char buf[1024];
#if (defined(WIN32) || defined(WIN64)) && (_MSC_VER > 1310)
    if (doShadow) {
        sprintf_s(buf, 1024, "Shaders/Atmosphere.cg");
    } else {
        sprintf_s(buf, 1024, "Shaders/Atmosphere-noshadow.cg");
    }
#else
    if (doShadow) {
        sprintf(buf, "Shaders/Atmosphere.cg");
    } else {
        sprintf(buf, "Shaders/Atmosphere-noshadow.cg");
    }
#endif

    atmoShader = renderer->LoadShaderFromFile(buf);
}

ShaderHandle AtmosphereFromSpaceShader::GetShader(void) const
{
    return atmoShader;
}

AtmosphereFromSpaceShader::~AtmosphereFromSpaceShader()
{
    Renderer* renderer = tcsData->GetRenderer();
    renderer->DeleteShader(atmoShader);
}
}