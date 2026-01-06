// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.
#include "GlareShader.h"

#include "ThreadCameraStreamData.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
GlareShader::GlareShader(ThreadCameraStreamData* _tcsData)
    : tcsData(_tcsData)
{
    Renderer* renderer = tcsData->GetRenderer();
    shader = renderer->LoadShaderFromFile("Shaders/Glare.cg");
}

ShaderHandle GlareShader::GetShader(void) const
{
    return shader;
}

GlareShader::~GlareShader()
{
    Renderer* renderer = tcsData->GetRenderer();
    renderer->DeleteShader(shader);
}

bool GlareShader::Bind(void)
{
    if (shader) {
        Renderer* renderer = tcsData->GetRenderer();
        if (renderer->BindShader(shader)) {
            return true;
        }
    }
    return false;
}
void GlareShader::UnBind(void)
{
    Renderer* renderer = tcsData->GetRenderer();
    renderer->UnbindShader();
}
}