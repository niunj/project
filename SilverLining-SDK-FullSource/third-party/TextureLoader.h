#ifndef _TEXTURE_LOADER_H_
#define _TEXTURE_LOADER_H_

#include "MemAlloc.h"

#ifdef LINUX
#define GL_GLEXT_LEGACY
#endif

#if defined(WIN32) || defined(WIN64)
#include <windows.h>										// Header File For Windows
#else
#define TRUE GL_TRUE
#define FALSE GL_FALSE
#define BOOL GLboolean
#define BYTE GLubyte
#define lstrcpy strcpy
#define lstrlen strlen
#endif

#ifdef ANDROID
#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#else
#ifdef MAC

// Use the latest glext.h that we include
#define GL_GLEXT_LEGACY
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#include "glext.h"
#else
#include "GL/gl.h"											// Header File For The OpenGL32 Library
#include "GL/glu.h"											// Header File For The GLu32 Library
#endif
#endif

#include "ResourceLoader.h"

#if defined(WIN32) || defined(WIN64)
#include <olectl.h>											// Header File For The OLE Controls Library
#endif

#include <math.h>											// Header File For The Math Library
#include <stdio.h>											// Header File For I/O Library

// Define Interface Data Types / Structures
//
typedef enum {
	txUnknown	= 0,	// images
	txBmp		= 1,
	txJpg		= 2,
	txPng		= 3,
	txTga		= 4,
	txGif		= 5,
	txIco		= 6,
	txEmf		= 7,
	txWmf		= 8,
	// add new ones at the end
} eglTexType;

typedef enum {
	txNoFilter	= 0,
	txBilinear	= 1,
	txTrilinear	= 2,
	// add new ones at the end
} eglTexFilterType;

class glTexture : public SilverLining::MemObject
{
public:
    glTexture() : TextureID(0), TexType(txUnknown), Width(0), Height(0),
        Type(0), InternalType(0), Bpp(0) {}

	GLuint		TextureID;									// Texture ID Used To Select A Texture
	eglTexType	TexType;									// Texture Format
	GLuint		Width;										// Image Width
	GLuint		Height;										// Image Height
	GLuint		Type;										// Image Type (GL_RGB, GL_RGBA)
	GLuint		InternalType;								// Internal storage type (GL_COMPRESSED_RGB etc)
	GLuint		Bpp;										// Image Color Depth In Bits Per Pixel
};

typedef struct {
	float	s;
	float	t;
} _glTexturCord;


typedef struct {
	_glTexturCord TopRight;
	_glTexturCord TopLeft;
	_glTexturCord BottomLeft;
	_glTexturCord BottomRight;
} glTexturCordTable;


// define TextureLoader Class
//
class TextureLoader : public SilverLining::MemObject
{
public:
	// methods
						TextureLoader();
	virtual				~TextureLoader();
	void				SetAlphaMatch(GLboolean fEnabled, GLubyte RedAlphaMatch, GLubyte GreenAlphaMatch, GLubyte BlueAlphaMatch);
	void				SetHighQualityTextures(GLboolean fEnabled);
	void				SetMipMapping(GLboolean fEnabled);
	void				SetTextureFilter(eglTexFilterType type);

    int					LoadTextureFromDisk(const char *szFileName, glTexture *pglTexture, SilverLining::ResourceLoader *resourceLoader);
	void				FreeTexture(glTexture *pglTexture);
	// variables

private:
	// methods
    int					LoadTGAFromDisk(char *pszFileName, glTexture *pglTexture, SilverLining::ResourceLoader *resourceLoader);

	int					GenerateTexture(glTexture *pglTexture, GLubyte *pImgData);

	void				ExtensionFromFilename(const char *szFileName, char *szExtension);

	// variables
	GLboolean			m_fAlphaConversion;
	GLboolean			m_fHighQualityTextures;
	GLboolean			m_fMipMapping;
	eglTexFilterType	m_TextureFilterType;

	GLubyte				m_RedAlphaMatch;
	GLubyte				m_GreenAlphaMatch;
	GLubyte 			m_BlueAlphaMatch;
};

#endif // _TEXTURE_LOADER_H_

