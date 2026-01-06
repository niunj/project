// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "SilverLiningOpenGLPreamble.h"
#include "Shader.h"
#include "SL_Buffer.h"
#include "BufferGL.h"
#include "OpenGLUtils.h"

#include <string.h>
#include <vector>

#if ((defined(WIN32) || defined(WIN64)) && _MSC_VER >= 1600)
#define SILVERLINING_RESTRICT_KEYWORD __restrict
#else
#define SILVERLINING_RESTRICT_KEYWORD
#endif
namespace SilverLining
{

struct base;

typedef void (APIENTRYP PFN_EXECUTE)(const base* SILVERLINING_RESTRICT_KEYWORD pParams);

struct base {
    PFN_EXECUTE     pfnExecute;
};

struct GLENABLE : public base {
    GLenum cap;

    static void APIENTRY execute(const GLENABLE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glEnable(pParams->cap);
    }
};

struct GLDISABLE : public base {
    GLenum cap;

    static void APIENTRY execute(const GLDISABLE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDisable(pParams->cap);
    }
};

struct GLBEGIN : public base {
    GLenum cap;

    static void APIENTRY execute(const GLBEGIN* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBegin(pParams->cap);
    }
};


struct GLEND : public base {

    static void APIENTRY execute(const GLEND* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glEnd();
    }
};

struct GLPUSHMATRIX : public base {

    static void APIENTRY execute(const GLPUSHMATRIX* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glPushMatrix();
    }
};


struct GLPOPMATRIX : public base {

    static void APIENTRY execute(const GLPOPMATRIX* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glPopMatrix();
    }
};



struct GLPUSHATTRIB : public base {
    GLbitfield mask;

    static void APIENTRY execute(const GLPUSHATTRIB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glPushAttrib(pParams->mask);
    }
};

struct GLPOPATTRIB : public base {

    static void APIENTRY execute() {
        glPopAttrib();
    }
};


struct GLVERTEX3D : public base {
    GLdouble x;
    GLdouble y;
    GLdouble z;

    static void APIENTRY execute(const GLVERTEX3D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glVertex3d(pParams->x, pParams->y, pParams->z);
    }
};

struct GLCOLOR4D : public base {
    GLdouble red, green, blue, alpha;

    static void APIENTRY execute(const GLCOLOR4D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glColor4d(pParams->red, pParams->green, pParams->blue, pParams->alpha);
    }
};

struct GLCOLOR4F : public base {
    GLfloat red, green, blue, alpha;

    static void APIENTRY execute(const GLCOLOR4F* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glColor4f(pParams->red, pParams->green, pParams->blue, pParams->alpha);
    }
};

struct GLTEXCOORD2D : public base {
    GLdouble s, t;

    static void APIENTRY execute(const GLTEXCOORD2D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glTexCoord2d(pParams->s, pParams->t);
    }
};

struct GLMATRIXMODE : public base {
    GLenum mode;

    static void APIENTRY execute(const GLMATRIXMODE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glMatrixMode(pParams->mode);
    }
};

struct GLLOADIDENTITY : public base {

    static void APIENTRY execute(const GLLOADIDENTITY* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glLoadIdentity();
    }
};

struct GLLOADMATRIXD : public base {
    double m[16];
    static void APIENTRY execute(const GLLOADMATRIXD* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glLoadMatrixd(pParams->m);
    }
};

struct GLUORTHO2D : public base {

    GLdouble left, right, bottom, top;

    static void APIENTRY execute(const GLUORTHO2D* SILVERLINING_RESTRICT_KEYWORD pParams);
};

struct GLRASTERPOS2I : public base {

    GLint x, y;

    static void APIENTRY execute(const GLRASTERPOS2I* SILVERLINING_RESTRICT_KEYWORD pParams)
    {
        glRasterPos2i(pParams->x, pParams->y);
    }
};

struct GLDRAWPIXELS : public base {

    GLsizei width;
    GLsizei height;
    GLenum format;
    GLenum type;
    const void *pixels;

    static void APIENTRY execute(const GLDRAWPIXELS* SILVERLINING_RESTRICT_KEYWORD pParams)
    {
        glDrawPixels(pParams->width, pParams->height, pParams->format, pParams->type, pParams->pixels);
    }
};


struct GLTEXENVI : public base {
    GLenum target;
    GLenum pname;
    GLint param;

    static void APIENTRY execute(const GLTEXENVI* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glTexEnvi(pParams->target, pParams->pname, pParams->param);
    }
};

struct GLPOINTPARAMETERINV : public base {
    GLenum pname;
    GLint param;

    static void APIENTRY execute(const GLPOINTPARAMETERINV* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glPointParameteriNV(pParams->pname, pParams->param);
    }
};

struct GLPOINTSIZE : public base {
    GLfloat size;
    static void APIENTRY execute(const GLPOINTSIZE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glPointSize(pParams->size);
    }
};

struct GLBINDBUFFERARB : public base {
    GLenum target;
    SL_Buffer* slBuffer;

    static void APIENTRY execute(const GLBINDBUFFERARB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        const GLuint handle = (pParams->slBuffer) ? pParams->slBuffer->handle : 0;
        glBindBufferARB(pParams->target, handle);
    }
};

struct GLPSEUDOBINDBUFFERARB : public base {
    GLenum target;
    SL_Buffer* slBuffer;

    static void APIENTRY execute(const GLPSEUDOBINDBUFFERARB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        if (pParams->slBuffer&&pParams->slBuffer->handle == 0)
        {
            pParams->slBuffer->InitBuffers();
        }
        const GLuint handle = (pParams->slBuffer) ? pParams->slBuffer->handle : 0;
        glBindBufferARB(pParams->target, handle);
    }
};

struct GLBINDBUFFERARB2 : public base {
    GLenum target;
    BufferGL* slBuffer;

    static void APIENTRY execute(const GLBINDBUFFERARB2* SILVERLINING_RESTRICT_KEYWORD pParams) {
        GLuint handle = (pParams->slBuffer) ? pParams->slBuffer->Handle() : 0;
        glBindBufferARB(pParams->target, handle);
    }
};

struct GLENABLECLIENTSTATE : public base {
    GLenum array;

    static void APIENTRY execute(const GLENABLECLIENTSTATE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glEnableClientState(pParams->array);
    }
};


struct GLDISABLECLIENTSTATE : public base {
    GLenum array;

    static void APIENTRY execute(const GLDISABLECLIENTSTATE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDisableClientState(pParams->array);
    }
};

struct GLCOLORFORMATNV : public base {
    GLint size;
    GLenum type;
    GLsizei stride;

    static void APIENTRY execute(const GLCOLORFORMATNV* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glColorFormatNV(pParams->size, pParams->type, pParams->stride);
    }
};

struct GLVERTEXFORMATNV : public base {
    GLint size;
    GLenum type;
    GLsizei stride;

    static void APIENTRY execute(const GLVERTEXFORMATNV* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glVertexFormatNV(pParams->size, pParams->type, pParams->stride);
    }
};

struct GLTEXCOORDFORMATNV : public base {
    GLint size;
    GLenum type;
    GLsizei stride;

    static void APIENTRY execute(const GLTEXCOORDFORMATNV* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glTexCoordFormatNV(pParams->size, pParams->type, pParams->stride);
    }
};


struct GLDEPTHFUNC : public base {
    GLenum func;

    static void APIENTRY execute(const GLDEPTHFUNC* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDepthFunc(pParams->func);
    }
};

struct GLDEPTHRANGEDNV: public base {
    float zmin;
    float zmax;

    static void APIENTRY execute(const GLDEPTHRANGEDNV* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDepthRangedNV(pParams->zmin, pParams->zmax);
    }
};

struct GLDEPTHRANGE : public base {
    float zmin;
    float zmax;

    static void APIENTRY execute(const GLDEPTHRANGE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDepthRange(pParams->zmin, pParams->zmax);
    }
};

struct GLVERTEXPOINTER : public base {
    GLint size;
    GLenum type;
    GLsizei stride;
    const GLvoid *pointer;

    static void APIENTRY execute(const GLVERTEXPOINTER* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glVertexPointer(pParams->size, pParams->type, pParams->stride, pParams->pointer);
    }
};

struct GLCOLORPOINTER : public base {
    GLint size;
    GLenum type;
    GLsizei stride;
    const GLvoid *pointer;

    static void APIENTRY execute(const GLCOLORPOINTER* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glColorPointer(pParams->size, pParams->type, pParams->stride, pParams->pointer);
    }
};

struct GLTEXCOORDPOINTER : public base {
    GLint size;
    GLenum type;
    GLsizei stride;
    const GLvoid *pointer;

    static void APIENTRY execute(const GLTEXCOORDPOINTER* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glTexCoordPointer(pParams->size, pParams->type, pParams->stride, pParams->pointer);
    }
};


struct GLBUFFERADDRESSRANGENV : public base {
    GLenum pname;
    GLuint index;
    GLuint64EXT address;
    GLsizeiptr length;

    static void APIENTRY execute(const GLBUFFERADDRESSRANGENV* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBufferAddressRangeNV(pParams->pname, pParams->index, pParams->address, pParams->length);
    }
};

struct GLUSEPROGRAMOBJECTARB : public base {
    GLuint program;

    static void APIENTRY execute(const GLUSEPROGRAMOBJECTARB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUseProgramObjectARB((GLhandleARB)(pParams->program));
    }
};

struct GLBLENDFUNC : public base {
    GLenum sfactor;
    GLenum dfactor;

    static void APIENTRY execute(const GLBLENDFUNC* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBlendFunc(pParams->sfactor, pParams->dfactor);
    }
};


struct GLBLENDFUNCSEPARATE : public base {

    GLenum sfactorRGB;
    GLenum dfactorRGB;
    GLenum sfactorAlpha;
    GLenum dfactorAlpha;

    static void APIENTRY execute(const GLBLENDFUNCSEPARATE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBlendFuncSeparate(pParams->sfactorRGB, pParams->dfactorRGB, pParams->sfactorAlpha, pParams->dfactorAlpha);
    }
};

struct GLBLENDEQUATIONSEPARATE : public base {

    GLenum modeRGB;
    GLenum modeAlpha;

    static void APIENTRY execute(const GLBLENDEQUATIONSEPARATE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBlendEquationSeparate(pParams->modeRGB, pParams->modeRGB);
    }
};

struct GLFOGF : public base {

    GLenum pname;
    GLfloat param;

    static void APIENTRY execute(const GLFOGF* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glFogf(pParams->pname, pParams->param);
    }
};

struct GLFOGFV : public base {

    GLenum pname;
    GLfloat params[3];

    static void APIENTRY execute(const GLFOGFV* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glFogfv(pParams->pname, pParams->params);
    }
};

struct GLFOGI : public base {

    GLenum pname;
    GLint param;

    static void APIENTRY execute(const GLFOGI* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glFogi(pParams->pname, pParams->param);
    }
};

struct GLFRONTFACE : public base {
    GLenum mode;

    static void APIENTRY execute(const GLFRONTFACE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glFrontFace(pParams->mode);
    }
};


struct GLDEPTHMASK : public base {
    GLboolean flag;

    static void APIENTRY execute(const GLDEPTHMASK* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDepthMask(pParams->flag);
    }
};

struct GLDRAWARRAYS : public base {
    GLenum mode;
    GLint first;
    GLsizei count;

    static void APIENTRY execute(const GLDRAWARRAYS* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDrawArrays(pParams->mode, pParams->first, pParams->count);
    }
};

struct GLDRAWELEMENTS : public base {
    GLenum mode;
    GLsizei count;
    GLenum type;
    const GLvoid *indices;

    static void APIENTRY execute(const GLDRAWELEMENTS* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDrawElements(pParams->mode, pParams->count, pParams->type, pParams->indices);
    }
};

struct GLDRAWELEMENTSINSTANCED : public base {
    GLenum mode;
    GLsizei count;
    GLenum type;
    const GLvoid *indices;
    GLsizei instancecount;

    static void APIENTRY execute(const GLDRAWELEMENTSINSTANCED* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDrawElementsInstanced(pParams->mode, pParams->count, pParams->type, pParams->indices, pParams->instancecount);
    }
};

struct GLCOLORMASK : public base {
    GLboolean red;
    GLboolean green;
    GLboolean blue;
    GLboolean alpha;

    static void APIENTRY execute(const GLCOLORMASK* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glColorMask(pParams->red, pParams->green, pParams->blue, pParams->alpha);
    }
};


struct GLPOLYGONMODE : public base {
    GLenum face;
    GLenum mode;

    static void APIENTRY execute(const GLPOLYGONMODE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glPolygonMode(pParams->face, pParams->mode);
    }
};

struct GLACTIVETEXTURE : public base {
    GLenum texture;

    static void APIENTRY execute(const GLACTIVETEXTURE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glActiveTexture(pParams->texture);
    }
};

struct GLCLIENTACTIVETEXTURE : public base {
    GLenum texture;

    static void APIENTRY execute(const GLCLIENTACTIVETEXTURE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glClientActiveTexture(pParams->texture);
    }
};

struct GLBINDTEXTURE : public base {
    GLenum target;
    GLuint texture;

    static void APIENTRY execute(const GLBINDTEXTURE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBindTexture(pParams->target, pParams->texture);
    }
};

struct GLPIXELSTOREI : public base {
    GLenum pname;
    GLint param;

    static void APIENTRY execute(const GLPIXELSTOREI* SILVERLINING_RESTRICT_KEYWORD pParams) {
        OpenGLUtils::CheckError(__LINE__);
        glPixelStorei(pParams->pname, pParams->param);
        OpenGLUtils::CheckError(__LINE__);
    }
};

struct GLTEXSUBIMAGE3D : public base {

    GLenum target;
    GLint level;

    GLint xoffset;
    GLint yoffset;
    GLint zoffset;

    GLsizei width;
    GLsizei height;
    GLsizei depth;

    GLenum format;
    GLenum type;
    const void *pixels;

    static void APIENTRY execute(const GLTEXSUBIMAGE3D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        OpenGLUtils::CheckError(__LINE__);
        glTexSubImage3D(pParams->target, pParams->level
            , pParams->xoffset, pParams->yoffset, pParams->zoffset
            , pParams->width, pParams->height, pParams->depth
            , pParams->format, pParams->type, pParams->pixels);
        OpenGLUtils::CheckError(__LINE__);
    }
};

struct GLNAMEDBUFFERSUBDATA : public base {
    GLuint buffer;
    GLintptr offset;
    GLsizeiptr size;
    const void *data;

    static void APIENTRY execute(const GLNAMEDBUFFERSUBDATA* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glNamedBufferSubData(pParams->buffer, pParams->offset, pParams->size, pParams->data);
    }
};

struct GLPSEUDONAMEDBUFFERSUBDATA : public base {
    SL_Buffer* slBuffer;
    GLintptr offset;
    GLsizeiptr size;
    const void *data;

    static void APIENTRY execute(const GLPSEUDONAMEDBUFFERSUBDATA* SILVERLINING_RESTRICT_KEYWORD pParams);
};

struct GLPSEUDOSYNC : public base {
    SL_Buffer* slBuffer;
    GLintptr offset;
    GLsizeiptr size;
    bool justColors;
    int startIndexColors;
    std::vector<Vertex>* scratchVerticesWithColors;

    int startIndexCopyInto;
    std::vector<Vertex>* scratchVerticesToGetFromVertexBufferAndCopyColorsInto;

    static void APIENTRY execute(const GLPSEUDOSYNC* SILVERLINING_RESTRICT_KEYWORD pParams);
};

struct GLBUFFERSUBDATAARB : public base {
    GLenum target;
    GLintptr offset;
    GLsizeiptr size;
    const void *data;

    static void APIENTRY execute(const GLBUFFERSUBDATAARB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBufferSubDataARB(pParams->target, pParams->offset, pParams->size, pParams->data);
    }
};

struct GLBUFFERDATAARB : public base {
    GLenum target;
    GLsizeiptr size;
    const void *data;
    GLenum usage;

    static void APIENTRY execute(const GLBUFFERDATAARB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBufferDataARB(pParams->target, pParams->size, pParams->data, pParams->usage);
    }
};


struct GLDELETEBUFFERARB : public base {
    GLuint handle;

    static void APIENTRY execute(const GLDELETEBUFFERARB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        GLuint handle = pParams->handle;
        glDeleteBuffersARB(1, &handle);
    }
};

struct GLBINDBUFFERBASE : public base {
    GLenum target;
    GLuint index;
    GLuint buffer;

    static void APIENTRY execute(const GLBINDBUFFERBASE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBindBufferBase(pParams->target, pParams->index, pParams->buffer);
    }
};

struct GLPROGRAMUNIFORM1F : public base {
    GLuint program;
    GLint location;
    GLfloat v0;

    static void APIENTRY execute(const GLPROGRAMUNIFORM1F* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glProgramUniform1f(pParams->program, pParams->location, pParams->v0);
    }
};


struct GLPROGRAMUNIFORM2F : public base {
    GLuint program;
    GLint location;
    GLfloat v0;
    GLfloat v1;

    static void APIENTRY execute(const GLPROGRAMUNIFORM2F* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glProgramUniform2f(pParams->program, pParams->location, pParams->v0, pParams->v1);
    }
};

struct GLPROGRAMUNIFORM3F : public base {
    GLuint program;
    GLint location;
    GLfloat v0;
    GLfloat v1;
    GLfloat v2;

    static void APIENTRY execute(const GLPROGRAMUNIFORM3F* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glProgramUniform3f(pParams->program, pParams->location, pParams->v0, pParams->v1, pParams->v2);
    }
};

struct GLPROGRAMUNIFORM4F : public base {
    GLuint program;
    GLint location;
    GLfloat v0;
    GLfloat v1;
    GLfloat v2;
    GLfloat v3;

    static void APIENTRY execute(const GLPROGRAMUNIFORM4F* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glProgramUniform4f(pParams->program, pParams->location, pParams->v0, pParams->v1, pParams->v2, pParams->v3);
    }
};


/////////////////

struct GLPROGRAMUNIFORM1D : public base {
    GLuint program;
    GLint location;
    GLdouble v0;

    static void APIENTRY execute(const GLPROGRAMUNIFORM1D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glProgramUniform1d(pParams->program, pParams->location, pParams->v0);
    }
};


struct GLPROGRAMUNIFORM2D : public base {
    GLuint program;
    GLint location;
    GLdouble v0;
    GLdouble v1;

    static void APIENTRY execute(const GLPROGRAMUNIFORM2D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glProgramUniform2d(pParams->program, pParams->location, pParams->v0, pParams->v1);
    }
};

struct GLPROGRAMUNIFORM3D : public base {
    GLuint program;
    GLint location;
    GLdouble v0;
    GLdouble v1;
    GLdouble v2;

    static void APIENTRY execute(const GLPROGRAMUNIFORM3D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glProgramUniform3d(pParams->program, pParams->location, pParams->v0, pParams->v1, pParams->v2);
    }
};

struct GLPROGRAMUNIFORM4D : public base {
    GLuint program;
    GLint location;
    GLdouble v0;
    GLdouble v1;
    GLdouble v2;
    GLdouble v3;

    static void APIENTRY execute(const GLPROGRAMUNIFORM4D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glProgramUniform4d(pParams->program, pParams->location, pParams->v0, pParams->v1, pParams->v2, pParams->v3);
    }
};

struct GLPROGRAMUNIFORM1I : public base {
    GLuint program;
    GLint location;
    GLint v0;

    static void APIENTRY execute(const GLPROGRAMUNIFORM1I* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glProgramUniform1i(pParams->program, pParams->location, pParams->v0);
    }
};

struct GLUNIFORM4FARB : public base {
    GLint location;
    GLfloat v0;
    GLfloat v1;
    GLfloat v2;
    GLfloat v3;

    static void APIENTRY execute(const GLUNIFORM4FARB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniform4fARB(pParams->location, pParams->v0, pParams->v1, pParams->v2, pParams->v3);
    }
};


struct GLUNIFORMMATRIX4FVARB : public base {
    GLint location;
    GLsizei count;
    GLboolean transpose;
    GLfloat value[16];

    static void APIENTRY execute(const GLUNIFORMMATRIX4FVARB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniformMatrix4fvARB(pParams->location, pParams->count, pParams->transpose, pParams->value);
    }
};

struct GLPROGRAMUNIFORMMATRIX3FV : public base {
    GLuint program;
    GLint location;
    GLsizei count;
    GLboolean transpose;
    GLfloat value[9];

    static void APIENTRY execute(const GLPROGRAMUNIFORMMATRIX3FV* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glProgramUniformMatrix3fv(pParams->program, pParams->location, pParams->count, pParams->transpose, pParams->value);
    }
};

struct GLPROGRAMUNIFORMMATRIX4FV : public base {
    GLuint program;
    GLint location;
    GLsizei count;
    GLboolean transpose;
    GLfloat value[16];

    static void APIENTRY execute(const GLPROGRAMUNIFORMMATRIX4FV* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glProgramUniformMatrix4fv(pParams->program, pParams->location, pParams->count, pParams->transpose, pParams->value);
    }
};

struct GLTEXENVF : public base {
    GLenum target;
    GLenum pname;
    GLfloat param;

    static void APIENTRY execute(const GLTEXENVF* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glTexEnvf(pParams->target, pParams->pname, pParams->param);
    }
};



struct GLTEXSUBIMAGE2D : public base {
    GLenum target;
    GLint level;
    GLint xoffset;
    GLint yoffset;
    GLsizei width;
    GLsizei height;
    GLenum format;
    GLenum type;
    const void *pixels;

    static void APIENTRY execute(const GLTEXSUBIMAGE2D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glTexSubImage2D(pParams->target, pParams->level, pParams->xoffset, pParams->yoffset, pParams->width, pParams->height, pParams->format, pParams->type, pParams->pixels);
    }
};

struct GLGENERATEMIPMAP : public base {
    GLenum target;

    static void APIENTRY execute(const GLGENERATEMIPMAP* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glGenerateMipmap(pParams->target);
    }
};

struct GLBINDFRAMEBUFFER : public base {
    GLenum target;
    GLuint framebuffer;

    static void APIENTRY execute(const GLBINDFRAMEBUFFER* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBindFramebuffer(pParams->target, pParams->framebuffer);
    }
};

struct GLVIEWPORT : public base {
    GLint x;
    GLint y;
    GLsizei width;
    GLsizei height;

    static void APIENTRY execute(const GLVIEWPORT* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glViewport(pParams->x, pParams->y, pParams->width, pParams->height);
    }
};

struct GLHINT : public base {
    GLenum target;
    GLenum mode;

    static void APIENTRY execute(const GLHINT* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glHint(pParams->target, pParams->mode);
    }
};

struct GLLINEWIDTH : public base {
    GLfloat width;

    static void APIENTRY execute(const GLLINEWIDTH* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glLineWidth(pParams->width);
    }
};

struct GLCLEAR : public base {
    GLbitfield mask;

    static void APIENTRY execute(const GLCLEAR* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glClear(pParams->mask);
    }
};

struct GLCLEARCOLOR : public base {
    GLbitfield mask;
    GLclampf red, green, blue, alpha;
    

    static void APIENTRY execute(const GLCLEARCOLOR* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glClearColor(pParams->red, pParams->green, pParams->blue, pParams->alpha);
    }
};



struct GLPUSHCLIENTATTRIB : public base {
    GLbitfield mask;

    static void APIENTRY execute(const GLPUSHCLIENTATTRIB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glPushClientAttrib(pParams->mask);
    }
};



struct GLPOPCLIENTATTRIB : public base {

    static void APIENTRY execute() {
        glPopClientAttrib();
    }
};

struct GLDRAWBUFFER : public base {
    GLenum target;

    static void APIENTRY execute(const GLDRAWBUFFER* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDrawBuffer(pParams->target);
    }
};

struct GLBINDFRAMEBUFFEREXT : public base {
    GLenum target;
    GLuint framebuffer;

    static void APIENTRY execute(const GLBINDFRAMEBUFFEREXT* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBindFramebufferEXT(pParams->target, pParams->framebuffer);
    }
};

union ALL_PACKETS {
public:
    PFN_EXECUTE         execute;
private:
    base                Base;

    GLENABLE glEnable;

    GLDISABLE glDisable;

    GLBUFFERADDRESSRANGENV glBufferAddressRangeNV;


    GLENABLECLIENTSTATE glEnableClientState;

    GLDISABLECLIENTSTATE glDisableClientState;

    GLUSEPROGRAMOBJECTARB glUseProgramObjectARB;

    GLBLENDFUNC glBlendFunc;

    GLFRONTFACE glFrontFace;

    GLDEPTHMASK glDepthMask;

    GLDRAWELEMENTS glDrawElements;


    GLCOLORMASK glColorMask;


    GLPOLYGONMODE glPolygonMode;

    GLACTIVETEXTURE glActiveTexture;

    GLBINDTEXTURE glBindTexture;

    GLNAMEDBUFFERSUBDATA glNamedBufferSubData;


    GLBINDBUFFERBASE glBindBufferBase;

    GLPROGRAMUNIFORM1F glProgramUniform1f;
    GLPROGRAMUNIFORM2F glProgramUniform2f;
    GLPROGRAMUNIFORM3F glProgramUniform3f;
    GLPROGRAMUNIFORM4F glProgramUniform4f;


    /////////////////
    GLPROGRAMUNIFORM1D glProgramUniform1d;
    GLPROGRAMUNIFORM2D glProgramUniform2d;
    GLPROGRAMUNIFORM3D glProgramUniform3d;
    GLPROGRAMUNIFORM4D glProgramUniform4d;


    GLPROGRAMUNIFORM1I glProgramUniform1i;

    GLTEXENVF glTexEnvf;

    GLTEXENVI glTexEnvi;

    GLTEXSUBIMAGE2D glTexSubImage2D;

    GLGENERATEMIPMAP glGenerateMipmap;

    GLLOADMATRIXD glLoadMatrixd;
};

}