/****************************************************************************
*                                                                           *
* Texture Loader                                                            *
*                                                                           *
* Originally Based on Jeff Molofee's IPicture Basecode                      *
* Extensions By Chris Leathley (http://members.iinet.net.au/~cleathley/)    *
*                                                                           *
*****************************************************************************
*                                                                           *
*  Loads  : BMP, EMF, GIF, ICO, JPG, WMF and TGA                            *
*  Source : Reads From Disk, Ram, Project Resource or the Internet          *
*  Extras : Images Can Be Any Width Or Height                               *
*           Low Quality Textures can be created                             *
*           Different Filter Level Support (None, Bilinear and Trilinear    *
*           Mipmapping Support                                              *
*                                                                           *
*****************************************************************************
*                                                                           *
*  Free To Use In Projects Of Your Own.  All I Ask For Is A Simple Greet    *
*  Or Mention of my site in your readme or the project itself :)            *
*                                                                           *
*****************************************************************************
*                                                                           *
* Revision History                                                          *
*                                                                           *
* Version 1.0 Released                                                      *
* Version 1.1 Added FreeTexture and LoadTextureFromResource                 *
*             Added TexType to the glTexture Struction                      *
*             Optimisations to the Alpha Conversion Loop                    *
* Version 1.2 Added Support PSP8 TGA files...                               *
*             Single TGA file loader                                        *
* Version 1.3 Added Support for low quality textures..                      *
*             Added ScaleTGA function                                       *
* Version 1.4 Added Support for gluScaleImage                               *
*             Removed ScaleTGA (replacew with gluScaleImage)                *
*             Added TextureFilter and MipMapping Support                    *
*                                                                           *
****************************************************************************/

#include "TextureLoader.h"                                          // Our Header
#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include <stdlib.h>
#include <string.h>

#include "TGALoader.h"

#ifdef ANDROID
#include <android/log.h>
#define  LOG_TAG    "libsilverlining"
#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)
#endif

#ifndef ANDROID
#include "glext.h"

#if defined(WIN32) || defined(WIN64)
#define COMPRESS_TEXTURES
#include "wglext.h"
#elif defined(LINUX)
#include "GL/glx.h"
#define wglGetProcAddress(A) glXGetProcAddress((GLubyte *)A)
#elif defined(MAC)
#include <dlfcn.h>
static void * MyGLGetProcAddress (const char *name)
{
    static void *glHandle = NULL;
    void **handlePtr;
    void *addr = NULL;

    handlePtr = &glHandle;
    if (NULL == *handlePtr)
        *handlePtr = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_GLOBAL);
    if (NULL != *handlePtr)
        addr = dlsym(*handlePtr, name);

    return addr;
}
#define wglGetProcAddress(A) MyGLGetProcAddress(A)
#else
#error "Target platform not defined"
#endif
#endif

using namespace SilverLining;

// Constructor
//
TextureLoader::TextureLoader()
{
    SetAlphaMatch(FALSE, 0, 0, 0);                                      // Set the Alpha Matching State

    SetHighQualityTextures(TRUE);

    SetMipMapping(TRUE);

    // no texture filtering
    SetTextureFilter(txTrilinear);
}

// Destructor
//
TextureLoader::~TextureLoader()
{
}


// Set Alpha Matching State and Match Colour
//
void TextureLoader::SetAlphaMatch(GLboolean fEnabled, GLubyte RedAlphaMatch, GLubyte GreenAlphaMatch, GLubyte BlueAlphaMatch)
{
    m_fAlphaConversion  = fEnabled;
    // only set the colour match if the conversion is enabled
    if (fEnabled == TRUE) {
        m_RedAlphaMatch     = RedAlphaMatch;
        m_GreenAlphaMatch   = GreenAlphaMatch;
        m_BlueAlphaMatch    = BlueAlphaMatch;
    }
}


// set the high quality texture flag
//
void TextureLoader::SetHighQualityTextures(GLboolean fEnabled)
{
    m_fHighQualityTextures = fEnabled;
}


// set the mipmapping flag
//
void TextureLoader::SetMipMapping(GLboolean fEnabled)
{
    m_fMipMapping = fEnabled;
}


// set the texture filtering flag
//
void TextureLoader::SetTextureFilter(eglTexFilterType type)
{
    m_TextureFilterType = type;
}


// Load A Texture from Disk (based on the current location of the executable)
//
int TextureLoader::LoadTextureFromDisk(const char *szFileName, glTexture *pglTexture, ResourceLoader *resourceLoader)
{
    char szFullPath[1024];                                      // Full Path To Picture
    char szExtension[16];                                       // Extenstion of Picture

#if (_MSC_VER <= 1310 || LINUX || ANDROID)
    strcpy(szFullPath, szFileName);
#else
    strcpy_s(szFullPath, 1024, szFileName);
#endif

    ExtensionFromFilename(szFileName, szExtension);

    // if the file is a TGA then use the TGA file loader
#if defined(WIN32) || defined(WIN64)
    if (lstrcmpi(szExtension, "tga") == 0)
#else
    if (strcasecmp(szExtension, "tga") == 0)
#endif
    {
        return(LoadTGAFromDisk(szFullPath, pglTexture, resourceLoader));                // Load TGA (Compressed/Uncompressed)
    } else {
        return FALSE;
    }
}

// Free a Texture from openGL
//
void TextureLoader::FreeTexture(glTexture *pglTexture)
{
    glDeleteTextures(1, &pglTexture->TextureID);
}

// Load a TGA file
//
int TextureLoader::LoadTGAFromDisk(char *pszFileName, glTexture *pglTexture, ResourceLoader *resourceLoader)
{
    TGAHeader  header;
    GLubyte     *pImgData;
    GLint       glMaxTexDim;                                            // Holds Maximum Texture Size

    char *data;
    unsigned int dataLen;
    bool ok = resourceLoader->LoadResource(pszFileName, data, dataLen, false);

    if(!ok) {                                               // If it didn't open....
        return FALSE;                                                   // Exit function
    }

    char *p = data;
    memcpy(&header, p, sizeof(TGAHeader));
    p += sizeof(TGAHeader);

    // Precalc some values from the header
    const unsigned int imageType        = header.ImgType;
    const unsigned int imageWidth       = header.WidthLo  + header.WidthHi  * 256;
    const unsigned int imageHeight      = header.HeightLo + header.HeightHi * 256;
    unsigned int imageBytesPerPel   = header.Bpp / 8;
    const unsigned int imageSize        = imageWidth * imageHeight * imageBytesPerPel;

    // load up our texture information
    pglTexture->Width  = imageWidth;
    pglTexture->Height = imageHeight;
    pglTexture->Bpp    = header.Bpp;

    if(pglTexture->Bpp == 24) {                                         // If the BPP of the image is 24...
        pglTexture->Type = GL_RGB;                                      // Set Image type to GL_RGB
#ifdef COMPRESS_TEXTURES
        pglTexture->InternalType = GL_COMPRESSED_RGB;
#else
        pglTexture->InternalType = GL_RGB;
#endif
    } else if (pglTexture->Bpp == 8) {
#ifdef ANDROID
        pglTexture->Type = pglTexture->InternalType = GL_ALPHA;
#else
        pglTexture->Type = pglTexture->InternalType = GL_RED;
#endif
    } else { // Else if its 32 BPP
        pglTexture->Type = GL_RGBA;                                     // Set image type to GL_RGBA
#ifdef COMPRESS_TEXTURES
        pglTexture->InternalType = GL_COMPRESSED_RGBA;
#else
        pglTexture->InternalType = GL_RGBA;
#endif
    }

    // Validate header info
    if( ( imageType != 2 && imageType != 3 && imageType != 10 ) ||
            ( imageWidth == 0 ) || ( imageHeight == 0 ) ||
            ( imageBytesPerPel != 3 && imageBytesPerPel != 4 && imageBytesPerPel != 1) ) {
        // invalid header, bomb out
        resourceLoader->FreeResource(data);
        return (FALSE);
    }

    // Allocate the memory for the image size
    pImgData = (GLubyte *)SL_MALLOC(imageSize);

    if(pImgData == NULL) {                                              // If no space was allocated
        resourceLoader->FreeResource(data);
        return FALSE;                                                   // Return failed
    }

    // Skip image ident field
    if( header.ImgIdent > 0 ) {
        p += header.ImgIdent;
    }

    // un-compresses image ?
    if (imageType == 2 || imageType == 3) {
        memcpy(pImgData, p, imageSize);
        p += imageSize;

        // Byte Swapping Optimized By Steve Thomas
        if (imageType == 2) {
            for(GLuint cswap = 0; cswap < imageSize; cswap += imageBytesPerPel) {
                pImgData[cswap] ^= pImgData[cswap+2];
                pImgData[cswap+2] ^= pImgData[cswap];
                pImgData[cswap] ^= pImgData[cswap + 2];

                // pImgData[cswap] ^= pImgData[cswap+2] ^=
                // pImgData[cswap] ^= pImgData[cswap+2];
            }
        }
    } else {
        // compressed image
        GLuint pixelcount   = imageHeight * imageWidth;                 // Nuber of pixels in the image
        GLuint currentpixel = 0;                                        // Current pixel being read
        GLuint currentbyte  = 0;                                        // Current byte
        GLubyte * colorbuffer = (GLubyte *)SL_MALLOC(imageBytesPerPel); // Storage for 1 pixel

        do {
            GLubyte chunkheader = 0;                                        // Storage for "chunk" header

            memcpy(&chunkheader, p, sizeof(GLubyte));
            p += sizeof(GLubyte);

            if(chunkheader < 128) {                                         // If the ehader is < 128, it means the that is the number of RAW color packets minus 1
                // that follow the header
                chunkheader++;                                              // add 1 to get number of following color values
                for(short counter = 0; counter < chunkheader; counter++) {  // Read RAW color values
                    memcpy(colorbuffer, p, imageBytesPerPel);
                    p += imageBytesPerPel;
                    // write to memory
                    pImgData[currentbyte        ] = colorbuffer[2];     // Flip R and B vcolor values around in the process
                    pImgData[currentbyte + 1    ] = colorbuffer[1];
                    pImgData[currentbyte + 2    ] = colorbuffer[0];

                    if (imageBytesPerPel == 4) {                        // if its a 32 bpp image
                        pImgData[currentbyte + 3] = colorbuffer[3];     // copy the 4th byte
                    }

                    currentbyte += imageBytesPerPel;                    // Increase thecurrent byte by the number of bytes per pixel
                    currentpixel++;                                     // Increase current pixel by 1

                    if(currentpixel > pixelcount) {                     // Make sure we havent read too many pixels
                        resourceLoader->FreeResource(data);                             // Close file

                        if(colorbuffer != NULL) {                       // If there is data in colorbuffer
                            SL_FREE(colorbuffer);                           // Delete it
                        }

                        if(pImgData != NULL) {                          // If there is Image data
                            SL_FREE(pImgData);                              // delete it
                        }

                        return FALSE;                                   // Return failed
                    }
                }
            } else {                                                    // chunkheader > 128 RLE data, next color reapeated chunkheader - 127 times
                chunkheader -= 127;                                     // Subtract 127 to get rid of the ID bit

                memcpy(colorbuffer, p, imageBytesPerPel);
                p += imageBytesPerPel;

                for(short counter = 0; counter < chunkheader; counter++) { // copy the color into the image data as many times as dictated
                    // by the header
                    pImgData[currentbyte        ] = colorbuffer[2];     // switch R and B bytes areound while copying
                    pImgData[currentbyte + 1    ] = colorbuffer[1];
                    pImgData[currentbyte   + 2  ] = colorbuffer[0];

                    if(imageBytesPerPel == 4) {                         // If TGA images is 32 bpp
                        pImgData[currentbyte + 3] = colorbuffer[3];     // Copy 4th byte
                    }

                    currentbyte += imageBytesPerPel;                    // Increase current byte by the number of bytes per pixel
                    currentpixel++;                                     // Increase pixel count by 1

                    if(currentpixel > pixelcount) {                     // Make sure we havent written too many pixels
                        resourceLoader->FreeResource(data);                             // Close file

                        if(colorbuffer != NULL) {                       // If there is data in colorbuffer
                            SL_FREE(colorbuffer);                           // Delete it
                        }

                        if(pImgData != NULL) {                          // If there is Image data
                            SL_FREE(pImgData);                              // delete it
                        }

                        return FALSE;                                   // Return failed
                    }
                } // for(counter)
            } // if(chunkheader)
        } while(currentpixel < pixelcount);                             // Loop while there are still pixels left

        if (colorbuffer != NULL) {                       // If there is data in colorbuffer
            SL_FREE(colorbuffer);                           // Delete it
        }
    } // if (imageType == 2)

    resourceLoader->FreeResource(data);                                                 // Close the TGA file

    /*
    ** Scale Image to be a power of 2
    */

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &glMaxTexDim);                   // Get Maximum Texture Size Supported

    int lWidthPixels  = imageWidth;
    int lHeightPixels = imageHeight;

    // Resize Image To Closest Power Of Two
    if (lWidthPixels <= glMaxTexDim) // Is Image Width Less Than Or Equal To Cards Limit
        lWidthPixels = 1 << (int)floor((log((double)lWidthPixels)/log(2.0f)) + 0.5f);
    else  // Otherwise  Set Width To "Max Power Of Two" That The Card Can Handle
        lWidthPixels = glMaxTexDim;

    if (lHeightPixels <= glMaxTexDim) // Is Image Height Greater Than Cards Limit
        lHeightPixels = 1 << (int)floor((log((double)lHeightPixels)/log(2.0f)) + 0.5f);
    else  // Otherwise  Set Height To "Max Power Of Two" That The Card Can Handle
        lHeightPixels = glMaxTexDim;

    // if low quality textures then make them halve the size which saved 4 times the texture space
    if ((m_fHighQualityTextures == FALSE) && (lWidthPixels > 64)) {
        lWidthPixels /= 2;
        lHeightPixels /= 2;
    }

    // GL_LUMINANCE no longer supported in OpenGL 3.2, so convert 8-bit image to 24-bit.
    if (imageBytesPerPel == 1) {
        imageBytesPerPel = 3;
        pglTexture->Type = GL_RGB;
#ifdef COMPRESS_TEXTURES
        pglTexture->InternalType = GL_COMPRESSED_RGB;
#else
        pglTexture->InternalType = GL_RGB;
#endif
        GLubyte *buf = (GLubyte *)SL_MALLOC(imageWidth * imageHeight * imageBytesPerPel);
        GLubyte *src = pImgData;
        GLubyte *dst = buf;
        for (unsigned int row = 0; row < imageHeight; row++) {
            for (unsigned int col = 0; col < imageWidth; col++) {
                GLubyte p = *src++;
                *dst++ = p;
                *dst++ = p;
                *dst++ = p;
            }
        }
        SL_FREE(pImgData);
        pImgData = buf;
    }

#ifndef ANDROID
    // if the size needs to change, the rescale the raw image data
    if ( (lWidthPixels  != (int)imageWidth) &&
            (lHeightPixels != (int)imageHeight) ) {
        // allocated the some memory for the new texture
        GLubyte *pNewImgData = (GLubyte *)SL_MALLOC(lWidthPixels * lHeightPixels * imageBytesPerPel);

        GLenum format;
        if (imageBytesPerPel == 4) {
            format = GL_RGBA;
        } else if (imageBytesPerPel == 1) {
            format = GL_RED;
        } else {
            format = GL_RGB;
        }
#ifndef NO_GLU
        gluScaleImage(format, imageWidth, imageHeight, GL_UNSIGNED_BYTE, pImgData,
                      lWidthPixels, lHeightPixels, GL_UNSIGNED_BYTE, pNewImgData);
#endif
        // free the original image data
        SL_FREE(pImgData);

        // old becomes new..
        pImgData = pNewImgData;

        // update our texture structure
        pglTexture->Width  = lWidthPixels;
        pglTexture->Height = lHeightPixels;
    }
#endif

    // Typical Texture Generation Using Data From The TGA loader
    glGenTextures(1, &pglTexture->TextureID);                       // Create The Texture
    // generate the texture using the filtering model selected
    (void)GenerateTexture(pglTexture, (BYTE *)pImgData);

    // free the memory allocated
    SL_FREE(pImgData);

    return TRUE;                                                        // All went well, continue on
}


// Set the Texture parameters to match the type of filtering we want.
//
int TextureLoader::GenerateTexture(glTexture *pglTexture, GLubyte *pImgData)
{
    //int result = 0;

    // Typical Texture Generation Using Data From The Bitmap
    glBindTexture(GL_TEXTURE_2D, pglTexture->TextureID);                // Bind To The Texture ID

    BOOL    Mipping;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);

    switch(m_TextureFilterType) {
    default:
    case txNoFilter:
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1 );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        Mipping = FALSE;
        break;

    case txBilinear:
        if (m_fMipMapping == FALSE) {
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            // set the build type flag
            Mipping = FALSE;
        } else {
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
            Mipping = TRUE;
        }
        break;

    case txTrilinear:
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        // always mip mapping for trilinear
        Mipping = TRUE;
        break;
    }

    // crank out the texture
    glHint(GL_TEXTURE_COMPRESSION_HINT, GL_NICEST);

    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 pglTexture->InternalType,
                 pglTexture->Width,
                 pglTexture->Height,
                 0,
                 pglTexture->Type,
                 GL_UNSIGNED_BYTE,
                 pImgData);

    if (Mipping) {
#ifndef ANDROID
        // Build Mipmaps (builds different versions of the picture for distances - looks better)
        PFNGLGENERATEMIPMAPEXTPROC glGenerateMipmap = (PFNGLGENERATEMIPMAPEXTPROC)wglGetProcAddress("glGenerateMipmapEXT");
        if (glGenerateMipmap) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }
#else
        glGenerateMipmap(GL_TEXTURE_2D);
#endif
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    return 0;
}


// extract the extension from the specified filename
//
void TextureLoader::ExtensionFromFilename(const char *szFileName, char *szExtension)
{
    int len = lstrlen(szFileName);

    int begin;

    for (begin=len; begin>=0; begin--) {
        if (szFileName[begin] == '.') {
            begin++;
            break;
        }
    }

    if (begin <= 0) {
        szExtension[0] = '\0';
    } else {
        lstrcpy(szExtension, &szFileName[begin]);
    }
}

