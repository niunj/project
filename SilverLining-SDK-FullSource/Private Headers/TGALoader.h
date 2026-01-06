// Copyright (c) 2006-2023 Sundog Software, LLC. All rights reserved worldwide.

/**
    \file TGALoader.h
    \brief Loads a TGA file into a byte array.
 */

#ifndef TGA_LOADER_H
#define TGA_LOADER_H

#include "MemAlloc.h"

namespace SilverLining
{
typedef struct {
    unsigned char ImgIdent;
    unsigned char ignored[1];
    unsigned char ImgType;
    unsigned char ignored2[9];
    unsigned char WidthLo;
    unsigned char WidthHi;
    unsigned char HeightLo;
    unsigned char HeightHi;
    unsigned char Bpp;
    unsigned char ignored3[1];
} TGAHeader;

class ResourceLoader;

/** Loads a TGA file into a byte array. */
class TGALoader : public MemObject
{
public:
/** Default constructor. */
    TGALoader();

/** Virtual destructor. */
    virtual ~TGALoader();

public:
/** Loads the specified TGA graphics file.
   \param filename The full path to the TGA file to load.
   \return True if the operation succeeded.
 */
    bool Load(const char *filename, ResourceLoader* resourceLoader);

/** Returns a pointer to the interleaved RGB or RGBA pixels
   that make up this image. Assumes Load() has been called. */
    unsigned char *GetPixels() {
        return pixels;
    }

/** Returns the width of the image, in pixels. Assumes Load()
   has been called. */
    int GetWidth() const {
        return width;
    }

/** Returns the height of the image, in pixels. Assumes Load()
   has been called. */
    int GetHeight() const {
        return height;
    }

/** Returns the number of bits per pixel. ie, 32 for RGBA images,
   or 24 for RGB images. Assumes Load() has been called. */
    int GetBitsPerPixel() const {
        return bitsPerPixel;
    }
/** Returns the number of bytes per pixel */
    int GetBytesPerPixel() const {
        return bitsPerPixel/8;
    }
/** Convert rgb to rgba */
    void ConvertToRGBA(void);
private:
    unsigned char *pixels;
    int width, height, bitsPerPixel;
};
}

#endif
