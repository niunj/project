#include "OpenGLUtils.h"
#include "SilverLiningOpenGLPreamble.h"
#include <cstdio>

#ifdef MAC
#include <OpenGL/glu.h>
#else
#include "GL/glu.h"
#endif

namespace SilverLining
{
bool OpenGLUtils::enableDebugOutput = false;
void OpenGLUtils::CheckError(int lineNum)
{
    if (enableDebugOutput) {
        GLenum errCode = glGetError();
        if (errCode != GL_NO_ERROR) {
            printf("GL error line %d: %s\n", lineNum, gluErrorString(errCode));
        }
    }
}

void OpenGLUtils::ClearErrors()
{
    if (enableDebugOutput) {
        GLenum errCode = glGetError();
        while (errCode != GL_NO_ERROR) {
            errCode = glGetError();
        }
    }
}

bool OpenGLUtils::QueryError()
{

    GLenum errCode = glGetError();
    if (errCode != GL_NO_ERROR) {
        return false;
    }

    return true;
}

}
