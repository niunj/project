#pragma once
namespace SilverLining
{
    class OpenGLExtensionManager
    {
    public:
        static bool LoadGLExtensions(void);
        static bool ExtensionSupported(const char *extensionName);

        static bool HasUBOs();
        static bool HasProgramUniform();
        
        static bool usingFloatBuffers;
        static bool usingOcclusionQuery;
        static bool usingMultisample;
        static bool usingDepthRangedNV;
        static bool usingCoreProfile;
    };
}
