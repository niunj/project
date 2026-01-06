#pragma once

namespace SilverLining
{
    class OpenGLUtils
    {
    public:
        static bool CheckError(int line);
        static void ClearErrors();
        static bool QueryError();

        static bool enableDebugOutput;

    };
}