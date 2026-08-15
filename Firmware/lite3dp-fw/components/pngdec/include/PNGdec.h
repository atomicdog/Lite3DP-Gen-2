//
// PNG Decoder — ESP-IDF C-only port
//
// Original by Larry Bank (bitbank@pobox.com)
// Copyright 2021 BitBank Software, Inc. All Rights Reserved.
// Licensed under the Apache License, Version 2.0
//
// Modified for ESP-IDF: removed Arduino dependencies, C-only build
//
#ifndef __PNGDEC__
#define __PNGDEC__

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define memcpy_P memcpy
#define PROGMEM

/* Fixed buffer size for zlib inflate state.
 * inflate_state is ~7KB on 32-bit targets; 40KB total is sufficient. */
#define PNGDEC_ZLIB_SIZE 40960

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

#define PNG_FILE_BUF_SIZE 2048
#define PNG_MAX_BUFFERED_PIXELS (640*4 + 1)

// PNG filter type
enum {
    PNG_FILTER_NONE=0,
    PNG_FILTER_SUB,
    PNG_FILTER_UP,
    PNG_FILTER_AVG,
    PNG_FILTER_PAETH,
    PNG_FILTER_COUNT
};

// decode options
enum {
    PNG_CHECK_CRC = 1,
    PNG_FAST_PALETTE = 2
};

// source pixel type
enum {
    PNG_PIXEL_GRAYSCALE=0,
    PNG_PIXEL_TRUECOLOR=2,
    PNG_PIXEL_INDEXED=3,
    PNG_PIXEL_GRAY_ALPHA=4,
    PNG_PIXEL_TRUECOLOR_ALPHA=6
};

// RGB565 endianness
enum {
    PNG_RGB565_LITTLE_ENDIAN = 0,
    PNG_RGB565_BIG_ENDIAN
};

enum {
    PNG_MEM_RAM=0,
    PNG_MEM_FLASH
};

// Error codes
enum {
    PNG_SUCCESS = 0,
    PNG_INVALID_PARAMETER,
    PNG_DECODE_ERROR,
    PNG_MEM_ERROR,
    PNG_NO_BUFFER,
    PNG_UNSUPPORTED_FEATURE,
    PNG_INVALID_FILE,
    PNG_TOO_BIG
};

typedef struct png_draw_tag
{
    int y;
    int iWidth;
    int iPitch;
    int iPixelType;
    int iBpp;
    int iHasAlpha;
    void *pUser;
    uint8_t *pPalette;
    uint16_t *pFastPalette;
    uint8_t *pPixels;
} PNGDRAW;

typedef struct png_file_tag
{
    int32_t iPos;
    int32_t iSize;
    uint8_t *pData;
    void *fHandle;
} PNGFILE;

// Callback function prototypes
typedef int32_t (PNG_READ_CALLBACK)(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen);
typedef int32_t (PNG_SEEK_CALLBACK)(PNGFILE *pFile, int32_t iPosition);
typedef void * (PNG_OPEN_CALLBACK)(const char *szFilename, int32_t *pFileSize);
typedef void (PNG_DRAW_CALLBACK)(PNGDRAW *);
typedef void (PNG_CLOSE_CALLBACK)(void *pHandle);

typedef struct png_image_tag
{
    int iWidth, iHeight;
    uint8_t ucBpp, ucPixelType;
    uint8_t ucMemType;
    uint8_t *pImage;
    int iPitch;
    int iHasAlpha;
    int iInterlaced;
    uint32_t iTransparent;
    int iError;
    PNG_READ_CALLBACK *pfnRead;
    PNG_SEEK_CALLBACK *pfnSeek;
    PNG_OPEN_CALLBACK *pfnOpen;
    PNG_DRAW_CALLBACK *pfnDraw;
    PNG_CLOSE_CALLBACK *pfnClose;
    PNGFILE PNGFile;
    uint8_t ucZLIB[PNGDEC_ZLIB_SIZE];
    uint8_t ucPalette[1024];
    uint8_t ucPixels[PNG_MAX_BUFFERED_PIXELS * 2];
    uint8_t ucFileBuf[PNG_FILE_BUF_SIZE];
} PNGIMAGE;

// C API — PNG_openRAM and PNG_close are defined in png.inl
// PNG_STATIC is defined by the .c file that includes png.inl

int PNG_openRAM(PNGIMAGE *pPNG, uint8_t *pData, int iDataSize, PNG_DRAW_CALLBACK *pfnDraw);
int PNG_openFileCallbacks(PNGIMAGE *pPNG, const char *szFilename,
                          PNG_OPEN_CALLBACK *pfnOpen, PNG_CLOSE_CALLBACK *pfnClose,
                          PNG_READ_CALLBACK *pfnRead, PNG_SEEK_CALLBACK *pfnSeek,
                          PNG_DRAW_CALLBACK *pfnDraw);
int PNG_getWidth(PNGIMAGE *pPNG);
int PNG_getHeight(PNGIMAGE *pPNG);
int PNG_decode(PNGIMAGE *pPNG, void *pUser, int iOptions);
void PNG_close(PNGIMAGE *pPNG);
int PNG_getLastError(PNGIMAGE *pPNG);
int PNG_getBpp(PNGIMAGE *pPNG);
int PNG_getBufferSize(PNGIMAGE *pPNG);
uint8_t *PNG_getPalette(PNGIMAGE *pPNG);
int PNG_getPixelType(PNGIMAGE *pPNG);
int PNG_hasAlpha(PNGIMAGE *pPNG);
int PNG_isInterlaced(PNGIMAGE *pPNG);
uint8_t *PNG_getBuffer(PNGIMAGE *pPNG);
void PNG_setBuffer(PNGIMAGE *pPNG, uint8_t *pBuffer);
void PNG_getLineAsRGB565(PNGDRAW *pDraw, uint16_t *pPixels, int iEndianness, uint32_t u32Bkgd, int iHasAlpha);

#define INTELSHORT(p) ((*p) + (*(p+1)<<8))
#define INTELLONG(p) ((*p) + (*(p+1)<<8) + (*(p+2)<<16) + (*(p+3)<<24))
#define MOTOSHORT(p) (((*(p))<<8) + (*(p+1)))
#define MOTOLONG(p) (((*p)<<24) + ((*(p+1))<<16) + ((*(p+2))<<8) + (*(p+3)))

#define REGISTER_WIDTH 32

#endif // __PNGDEC__
