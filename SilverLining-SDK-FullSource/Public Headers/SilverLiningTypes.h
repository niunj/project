// Copyright (c) 2010-2013 Sundog Software, LLC. All rights reserved worldwide.

#ifndef SILVERLINING_TYPES_H
#define SILVERLINING_TYPES_H

/** Types of shaders, either a vertex or a fragment shader. */
enum ShaderTypes
{
    VERTEX_PROGRAM,
    FRAGMENT_PROGRAM,
    GEOMETRY_PROGRAM
};

/** Blend modes for blending equation coefficients. */
enum BlendFactor
{
    ZERO = 0,
    ONE = 1,
    SRCCOLOR = 2,
    INVSRCCOLOR = 3,
    SRCALPHA = 4,
    INVSRCALPHA = 5,
    DSTCOLOR = 6,
    INVDSTCOLOR = 7,
    DSTALPHA = 8,
    INVDSTALPHA = 9,
    SRCALPHASAT = 10,
    NUM_BLEND_FACTORS = 11
};

enum BlendOp
{
    BLEND_OP_ADD = 0,
    BLEND_OP_SUBTRACT = 1,
    BLEND_OP_REVERSE_SUBTRACT = 2,
    BLEND_OP_MIN = 3,
    BLEND_OP_MAX = 4,
    NUM_BLEND_OPS = 5
};

enum FrontFace
{
    SL_FRONT_FACE_COUNTER_CLOCKWISE = 0
  , SL_FRONT_FACE_CLOCKWISE = 1
  , SL_FRONT_FACE_NUM = 2
};

/** Cubemap faces */
enum CubeFace
{
    POSX, 
    NEGX, 
    POSY, 
    NEGY, 
    POSZ, 
    NEGZ
};

/** Sky models */
enum SkyModel
{
    PREETHAM,
    HOSEK_WILKIE
};

enum LightningDischargeMode
{
  AUTO_DISCHARGE,
  FORCE_ON_OFF,
  FIRE_AND_FORGET,
  MAX_DISCHARGE_MODE,
};
namespace SilverLining
{
    typedef unsigned int Index;
    typedef void* IndexBufferHandle;
    typedef void* VertexBufferHandle;
    typedef void* ShaderHandle;
    typedef void* TextureHandle;
    typedef void* RenderTextureHandle;
    typedef void* RenderTargetHandle;
    typedef void* QueryHandle;
    typedef void* CameraHandle;
    typedef void* BufferHandle;
    typedef void* CommandBufferHandle;
    typedef unsigned long long GPUAddressType;
    typedef unsigned long long GPULengthType;

    enum ColorFormat
    {
        FORMAT_UNKNOWN = 0
        , FORMAT_R8G8B8A8_UNORM = 1
        , FORMAT_R16G16B16A16_SFLOAT = 2
    };
}

#endif
