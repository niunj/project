#pragma once
namespace SilverLining
{
    class OpenGLExtensionManager
    {
    public:
        static int ExtensionSupported(const char *extension);
        
        static bool LoadGLExtensions(void);

        static bool HasUBOs();
        static bool HasProgramUniform(void);

    public:
        static bool usingVB;
        static bool usingPB;
        static bool usingFramebuffers;
        static bool usingNVPointSprite;
        static bool usingFloatBuffers;
        static bool usingBindlessGraphics;
        static bool usingOcclusionQuery;
        static bool usingGLSL;
        static bool usingMultisample;
        static bool usingDepthRangedNV;

        static bool isATI;
        static bool hasBindlessGraphics;
    };
}
