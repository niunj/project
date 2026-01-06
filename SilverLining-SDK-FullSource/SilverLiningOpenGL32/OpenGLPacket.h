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

struct GLBINDVERTEXARRAY : public base {
    GLuint array;

    static void APIENTRY execute(const GLBINDVERTEXARRAY* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBindVertexArray(pParams->array);
    }
};

struct GLBINDVERTEXARRAYFOR : public base {
    GLuint vboID;
    Shader* shader;

    static void APIENTRY execute(const GLBINDVERTEXARRAYFOR* SILVERLINING_RESTRICT_KEYWORD pParams) {
        GLuint vao = pParams->shader->GetOrCreateVAOForContextAndBuffer(pParams->vboID);
        glBindVertexArray(vao);
    }
};

struct GLBINDBUFFER : public base {
    GLenum target;
    SL_Buffer* slBuffer;

    static void APIENTRY execute(const GLBINDBUFFER* SILVERLINING_RESTRICT_KEYWORD pParams) {
        GLuint handle = (pParams->slBuffer) ? pParams->slBuffer->handle : 0;
        glBindBuffer(pParams->target, handle);
    }
};

struct GLBINDBUFFER2 : public base {
    GLenum target;
    BufferGL* slBuffer;

    static void APIENTRY execute(const GLBINDBUFFER2* SILVERLINING_RESTRICT_KEYWORD pParams) {
        GLuint handle = (pParams->slBuffer) ? pParams->slBuffer->Handle() : 0;
        glBindBuffer(pParams->target, handle);
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

struct GLUSEPROGRAM : public base {
    GLuint program;

    static void APIENTRY execute(const GLUSEPROGRAM* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUseProgram(pParams->program);
    }
};

struct GLVERTEXATTRIBPOINTER : public base {
    GLuint index;
    GLint size;
    GLenum type;
    GLboolean normalized;
    GLsizei stride;
    const void *pointer;

    static void APIENTRY execute(const GLVERTEXATTRIBPOINTER* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glVertexAttribPointer(pParams->index, pParams->size, pParams->type, pParams->normalized, pParams->stride, pParams->pointer);
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

struct GLENABLEVERTEXATTRIBARRAY : public base {
    GLuint index;

    static void APIENTRY execute(const GLENABLEVERTEXATTRIBARRAY* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glEnableVertexAttribArray(pParams->index);
    }
};

struct GLDISABLEVERTEXATTRIBARRAY : public base {
    GLuint index;

    static void APIENTRY execute(const GLDISABLEVERTEXATTRIBARRAY* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDisableVertexAttribArray(pParams->index);
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

struct GLFRONTFACE : public base {
    GLenum mode;

    static void APIENTRY execute(const GLFRONTFACE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glFrontFace(pParams->mode);
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

struct GLMULTIDRAWARRAYS : public base {
    GLenum mode;
    const GLint* first;
    const GLsizei* count;
    GLsizei drawcount;

    static void APIENTRY execute(const GLMULTIDRAWARRAYS* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glMultiDrawArrays(pParams->mode, pParams->first, pParams->count, pParams->drawcount);
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

struct GLPOINTSIZE : public base {
    GLfloat size;
    static void APIENTRY execute(const GLPOINTSIZE* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glPointSize(pParams->size);
    }
};

struct GLMULTIDRAWELEMENTS : public base {
    GLenum mode;
    const GLsizei *count;
    GLenum type;
    const void *const*indices;
    GLsizei drawcount;

    static void APIENTRY execute(const GLMULTIDRAWELEMENTS* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glMultiDrawElements(pParams->mode, pParams->count, pParams->type, pParams->indices, pParams->drawcount);
    }
};

struct GLDRAWARRAYSINDIRECT : public base {
    GLenum mode;
    const void *indirect;

    static void APIENTRY execute(const GLDRAWARRAYSINDIRECT* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDrawArraysIndirect(pParams->mode, pParams->indirect);
    }
};

struct GLMULTIDRAWARRAYSINDIRECT : public base {
    GLenum mode;
    const void *indirect;
    GLsizei drawcount;
    GLsizei stride;

    static void APIENTRY execute(const GLMULTIDRAWARRAYSINDIRECT* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glMultiDrawArraysIndirect(pParams->mode, pParams->indirect, pParams->drawcount, pParams->stride);
    }
};

struct GLDRAWELEMENTSINDIRECT : public base {
    GLenum mode;
    GLenum type;
    const void *indirect;

    static void APIENTRY execute(const GLDRAWELEMENTSINDIRECT* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDrawElementsIndirect(pParams->mode, pParams->type, pParams->indirect);
    }
};

struct GLMULTIDRAWELEMENTSINDIRECT : public base {
    GLenum mode;
    GLenum type;
    const void *indirect;
    GLsizei drawcount;
    GLsizei stride;

    static void APIENTRY execute(const GLMULTIDRAWELEMENTSINDIRECT* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glMultiDrawElementsIndirect(pParams->mode, pParams->type, pParams->indirect, pParams->drawcount, pParams->stride);
    }
};

struct GLVERTEXATTRIBDIVISOR : public base {
    GLuint index;
    GLuint divisor;

    static void APIENTRY execute(const GLVERTEXATTRIBDIVISOR* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glVertexAttribDivisor(pParams->index, pParams->divisor);
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

struct GLBUFFERSUBDATA : public base {
    GLenum target;
    GLintptr offset;
    GLsizeiptr size;
    const void *data;

    static void APIENTRY execute(const GLBUFFERSUBDATA* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBufferSubData(pParams->target, pParams->offset, pParams->size, pParams->data);
    }
};

struct GLBUFFERDATA : public base {
    GLenum target;
    GLsizeiptr size;
    const void *data;
    GLenum usage;

    static void APIENTRY execute(const GLBUFFERDATA* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBufferData(pParams->target, pParams->size, pParams->data, pParams->usage);
    }
};

struct GLDELETEBUFFER : public base {
    GLuint handle;

    static void APIENTRY execute(const GLDELETEBUFFER* SILVERLINING_RESTRICT_KEYWORD pParams) {
        GLuint handle = pParams->handle;
        glDeleteBuffers(1, &handle);
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


/////////////////
struct GLUNIFORM1F : public base {
    GLint location;
    GLfloat v0;

    static void APIENTRY execute(const GLUNIFORM1F* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniform1f(pParams->location, pParams->v0);
    }
};

struct GLUNIFORM2F : public base {
    GLint location;
    GLfloat v0;
    GLfloat v1;

    static void APIENTRY execute(const GLUNIFORM2F* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniform2f(pParams->location, pParams->v0, pParams->v1);
    }
};

struct GLUNIFORM3F : public base {
    GLint location;
    GLfloat v0;
    GLfloat v1;
    GLfloat v2;

    static void APIENTRY execute(const GLUNIFORM3F* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniform3f(pParams->location, pParams->v0, pParams->v1, pParams->v2);
    }
};

struct GLUNIFORM4F : public base {
    GLint location;
    GLfloat v0;
    GLfloat v1;
    GLfloat v2;
    GLfloat v3;

    static void APIENTRY execute(const GLUNIFORM4F* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniform4f(pParams->location, pParams->v0, pParams->v1, pParams->v2, pParams->v3);
    }
};



/////////////////
struct GLUNIFORM1D : public base {
    GLint location;
    GLdouble v0;

    static void APIENTRY execute(const GLUNIFORM1D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniform1d(pParams->location, pParams->v0);
    }
};

struct GLUNIFORM2D : public base {
    GLint location;
    GLdouble v0;
    GLdouble v1;

    static void APIENTRY execute(const GLUNIFORM2D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniform2d(pParams->location, pParams->v0, pParams->v1);
    }
};

struct GLUNIFORM3D : public base {
    GLint location;
    GLdouble v0;
    GLdouble v1;
    GLdouble v2;

    static void APIENTRY execute(const GLUNIFORM3D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniform3d(pParams->location, pParams->v0, pParams->v1, pParams->v2);
    }
};

struct GLUNIFORM4D : public base {
    GLint location;
    GLdouble v0;
    GLdouble v1;
    GLdouble v2;
    GLdouble v3;

    static void APIENTRY execute(const GLUNIFORM4D* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniform4d(pParams->location, pParams->v0, pParams->v1, pParams->v2, pParams->v3);
    }
};

struct GLUNIFORM1I : public base {
    GLint location;
    GLint v0;

    static void APIENTRY execute(const GLUNIFORM1I* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniform1i(pParams->location, pParams->v0);
    }
};

struct GLPROGRAMUNIFORMHANDLEUI64ARB : public base {
    GLuint program;
    GLint location;
    GLuint64 value;

    static void APIENTRY execute(const GLPROGRAMUNIFORMHANDLEUI64ARB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glProgramUniformHandleui64ARB(pParams->program, pParams->location, pParams->value);
    }
};


struct GLUNIFORMHANDLEUI64ARB : public base {
    GLint location;
    GLuint64 value;

    static void APIENTRY execute(const GLUNIFORMHANDLEUI64ARB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniformHandleui64ARB(pParams->location, pParams->value);
    }
};

struct GLUNIFORMMATRIX3FV : public base {
    GLint location;
    GLsizei count;
    GLboolean transpose;
    GLfloat value[9];

    static void APIENTRY execute(const GLUNIFORMMATRIX3FV* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniformMatrix3fv(pParams->location, pParams->count, pParams->transpose, pParams->value);
    }
};

struct GLUNIFORMMATRIX4FV : public base {
    GLint location;
    GLsizei count;
    GLboolean transpose;
    GLfloat value[16];

    static void APIENTRY execute(const GLUNIFORMMATRIX4FV* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glUniformMatrix4fv(pParams->location, pParams->count, pParams->transpose, pParams->value);
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

struct GLTEXENVI : public base {
    GLenum target;
    GLenum pname;
    GLint param;

    static void APIENTRY execute(const GLTEXENVI* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glTexEnvi(pParams->target, pParams->pname, pParams->param);
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


struct GLPUSHATTRIB : public base {
    GLbitfield mask;

    static void APIENTRY execute(const GLPUSHATTRIB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glPushAttrib(pParams->mask);
    }
};

struct GLPUSHCLIENTATTRIB : public base {
    GLbitfield mask;

    static void APIENTRY execute(const GLPUSHCLIENTATTRIB* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glPushClientAttrib(pParams->mask);
    }
};

struct GLPOPATTRIB : public base {

    static void APIENTRY execute() {
        glPopAttrib();
    }
};

struct GLPOPCLIENTATTRIB : public base {

    static void APIENTRY execute() {
        glPopClientAttrib();
    }
};

struct GLBINDFRAMEBUFFEREXT : public base {
    GLenum target;
    GLuint framebuffer;

    static void APIENTRY execute(const GLBINDFRAMEBUFFEREXT* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glBindFramebufferEXT(pParams->target, pParams->framebuffer);
    }
};

struct GLDRAWBUFFER : public base {
    GLenum target;

    static void APIENTRY execute(const GLDRAWBUFFER* SILVERLINING_RESTRICT_KEYWORD pParams) {
        glDrawBuffer(pParams->target);
    }
};

union ALL_PACKETS {
public:
    PFN_EXECUTE         execute;
private:
    base                Base;

    GLENABLE glEnable;

    GLDISABLE glDisable;

    GLBINDVERTEXARRAY glBindVertexArray;

    // pseudo-command
    GLBINDVERTEXARRAYFOR glBindVertexArrayFor;

    GLBINDBUFFER glBindBuffer;

    GLBUFFERADDRESSRANGENV glBufferAddressRangeNV;

    GLENABLECLIENTSTATE glEnableClientState;

    GLDISABLECLIENTSTATE glDisableClientState;

    GLUSEPROGRAM gUseProgram;

    GLVERTEXATTRIBPOINTER glVertexAttribPointer;


    GLVERTEXPOINTER glVertexPointer;

    GLENABLEVERTEXATTRIBARRAY glEnableVertexAttribArray;

    GLDISABLEVERTEXATTRIBARRAY glDisableVertexAttribArray;

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


    /////////////////
    GLUNIFORM1F glUniform1f;
    GLUNIFORM2F glUniform2f;
    GLUNIFORM3F glUniform3f;
    GLUNIFORM4F glUniform4f;

    /////////////////
    GLUNIFORM1D glUniform1d;
    GLUNIFORM2D glUniform2d;
    GLUNIFORM3D glUniform3d;
    GLUNIFORM4D glUniform4d;

    GLUNIFORM1I glUniform1i;

    GLPROGRAMUNIFORMHANDLEUI64ARB glProgramUniformHandleui64ARB;

    GLUNIFORMMATRIX3FV glUniformMatrix3fv;

    GLUNIFORMMATRIX4FV glProgramUniformMatrix4fv;

    GLTEXENVF glTexEnvf;

    GLTEXENVI glTexEnvi;

    GLTEXSUBIMAGE2D glTexSubImage2D;

    GLGENERATEMIPMAP glGenerateMipmap;
};


}