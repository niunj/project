#include "OpenGLUtils.h"
#include "SilverLiningOpenGLPreamble.h"
#include <cstdio>

namespace SilverLining
{
bool OpenGLUtils::enableDebugOutput = false;

bool OpenGLUtils::CheckError(int line)
{
    bool ok = true;
#ifndef NO_GLU
    if (enableDebugOutput) {
        GLenum errCode = GL_NO_ERROR;
        do {
            errCode = glGetError();
            if (errCode != GL_NO_ERROR) {
                printf("GL Error at line %d: %s\n", line, gluErrorString(errCode));
                ok = false;
            }
        } while (errCode != GL_NO_ERROR);
    }
#endif
    return ok;
}
void OpenGLUtils::ClearErrors(void)
{
#ifndef NO_GLU
    if (enableDebugOutput) {
        GLenum errCode = GL_NO_ERROR;
        do {
            errCode = glGetError();
        } while (errCode != GL_NO_ERROR);
    }
#endif
}
bool OpenGLUtils::QueryError(void)
{
    bool ok = true;
    GLenum errCode = GL_NO_ERROR;
    do {
        errCode = glGetError();
        if (errCode != GL_NO_ERROR) {
            ok = false;
        }
    } while (errCode != GL_NO_ERROR);

    return ok;
}
}