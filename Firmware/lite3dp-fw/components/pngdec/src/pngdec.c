//
// PNG Decoder — ESP-IDF C-only implementation
//
// Original by Larry Bank (bitbank@pobox.com)
// Copyright 2021 BitBank Software, Inc. All Rights Reserved.
// Licensed under the Apache License, Version 2.0
//

#define PNG_STATIC static

/* Include zlib internals needed by png.inl */
#include "zutil.h"
#include "inftrees.h"
#include "inflate.h"

/* In C (unlike C++), struct tags don't auto-create typedefs.
 * png.inl uses sizeof(inflate_state) without 'struct' keyword. */
typedef struct inflate_state inflate_state;

// Tell png.inl to skip its incomplete C API stubs.
// We define this before including PNGdec.h so the header processes normally,
// then define __LINUX__ guard to skip png.inl's PNG_openFile (which uses
// undefined readFile/seekFile/closeFile), and also skip the basic C API
// which uses undefined readMem.
// We achieve this by pre-declaring the functions that png.inl's C API
// tries to reference, pointing them to the functions that DO exist.

#include "PNGdec.h"

// png.inl's C API (inside #ifndef __cplusplus) calls readMem which doesn't
// exist — it only defines readRAM, readFLASH, seekMem. We redirect readMem
// to readRAM via a macro before including png.inl.
#define readMem readRAM

// Forward declarations for internal functions defined in png.inl
static int PNGInit(PNGIMAGE *pPNG);
static int DecodePNG(PNGIMAGE *pImage, void *pUser, int iOptions);
static uint8_t PNGMakeMask(PNGDRAW *pDraw, uint8_t *pMask, uint8_t ucThreshold);
static void PNGRGB565(PNGDRAW *pDraw, uint16_t *pPixels, int iEndianness, uint32_t u32Bkgd, int iHasAlpha);

// Forward declarations for I/O functions used by PNG_openRAM in png.inl
static int32_t readRAM(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen);
static int32_t readFLASH(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen);
static int32_t seekMem(PNGFILE *pFile, int32_t iPosition);

// Include the core decode logic
#include "png.inl"

#undef readMem

// The C API stubs in png.inl (PNG_openRAM, PNG_close) are now compiled
// but they work because readMem was redirected to readRAM.
// PNG_openFile is only compiled under __LINUX__ which we don't define,
// so it's skipped. We provide our own version below.

// ── Our extended C API (file-based open with callbacks) ─────────

int PNG_openFileCallbacks(PNGIMAGE *pPNG, const char *szFilename,
                          PNG_OPEN_CALLBACK *pfnOpen, PNG_CLOSE_CALLBACK *pfnClose,
                          PNG_READ_CALLBACK *pfnRead, PNG_SEEK_CALLBACK *pfnSeek,
                          PNG_DRAW_CALLBACK *pfnDraw)
{
    memset(pPNG, 0, sizeof(PNGIMAGE));
    pPNG->pfnRead = pfnRead;
    pPNG->pfnSeek = pfnSeek;
    pPNG->pfnDraw = pfnDraw;
    pPNG->pfnOpen = pfnOpen;
    pPNG->pfnClose = pfnClose;
    pPNG->PNGFile.fHandle = (*pfnOpen)(szFilename, &pPNG->PNGFile.iSize);
    if (pPNG->PNGFile.fHandle == NULL) {
        /* Must not return 0 here: in this C API 0 is PNG_SUCCESS, so a
         * failed open would look identical to a good one and the caller
         * would decode through a NULL file handle. */
        pPNG->iError = PNG_INVALID_FILE;
        return PNG_INVALID_FILE;
    }
    return PNGInit(pPNG);   /* PNG_SUCCESS (0) on success */
}

// PNG_openRAM, PNG_close, PNG_getWidth, PNG_getHeight, PNG_getBpp,
// PNG_getPixelType, PNG_getLastError, PNG_getPalette, PNG_getBufferSize,
// PNG_isInterlaced, PNG_getBuffer, PNG_setBuffer
// are defined by png.inl's C API (inside #ifndef __cplusplus).

// These are NOT in png.inl's C API — we must provide them:
int PNG_decode(PNGIMAGE *pPNG, void *pUser, int iOptions)
{
    return DecodePNG(pPNG, pUser, iOptions);
}

int PNG_hasAlpha(PNGIMAGE *pPNG)
{
    return pPNG->iHasAlpha;
}

void PNG_getLineAsRGB565(PNGDRAW *pDraw, uint16_t *pPixels, int iEndianness, uint32_t u32Bkgd, int iHasAlpha)
{
    PNGRGB565(pDraw, pPixels, iEndianness, u32Bkgd, iHasAlpha);
}
