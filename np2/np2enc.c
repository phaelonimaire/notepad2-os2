/* np2enc.c - encoding detection and conversion via UniUconv.
 *
 * API per os2ref/unicode-conversion.md. Three things that reference makes
 * clear and that are easy to get wrong:
 *
 *  - UniUconvToUcs / UniUconvFromUcs take COUNTED buffers and ADVANCE the
 *    pointers they are given. The caller must therefore pass copies of its
 *    pointers and counts, or lose track of its own allocation.
 *  - The counts are in different units on each side: bytes on the code-page
 *    side, UniChar ELEMENTS on the Unicode side.
 *  - UCONV_E2BIG means "output full, resume where I stopped" and is a normal
 *    condition, not a failure; sizing the output generously up front avoids
 *    the resume loop entirely and is what this code does.
 */
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_DOSNLS
#include <os2.h>
#include <uconv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "np2enc.h"

static const char *aEncNames[NP2ENC_COUNT] = {
    "ANSI", "OEM (850)", "UTF-8", "UTF-8 with signature",
    "Unicode (UCS-2 LE)", "Unicode big endian (UCS-2 BE)"
};

const char *EncName(int iEnc)
{
    return (iEnc >= 0 && iEnc < NP2ENC_COUNT) ? aEncNames[iEnc] : "?";
}

/*--------------------------------------------------------------------------
 * Code-set names
 *
 * uconv.h offers macros like UTF_8 that expand to L"IBM-1208", relying on
 * wchar_t being 16 bits so a wide literal IS an array of UniChar. That holds
 * for the IBM compilers; GCC's wchar_t is 32-bit, so those macros would hand
 * UniCreateUconvObject a buffer of the wrong shape and it would fail to
 * recognise the code set. Building the UCS-2 name a byte at a time is
 * independent of the compiler's wchar_t and costs nothing.
 *------------------------------------------------------------------------*/

static void UcsName(const char *pszAscii, UniChar *pOut, int cchOut)
{
    int i = 0;
    while (pszAscii[i] && i < cchOut - 1) {
        pOut[i] = (UniChar)(unsigned char)pszAscii[i];
        i++;
    }
    pOut[i] = 0;
}

/* Open a conversion object for a numeric code page, or for a literal name. */
static BOOL OpenConv(UconvObject *pObj, unsigned long ulCp, const char *pszName,
                     char *pszErr, int cchErr)
{
    UniChar aName[32];
    int rc;

    if (pszName)
        UcsName(pszName, aName, 32);
    else if (UniMapCpToUcsCp(ulCp, aName, 32) != 0) {
        if (pszErr)
            sprintf(pszErr, "UniMapCpToUcsCp failed for code page %lu", ulCp);
        return FALSE;
    }

    rc = UniCreateUconvObject(aName, pObj);
    if (rc != 0) {
        if (pszErr)
            sprintf(pszErr, "UniCreateUconvObject rc=%d for %s%lu",
                    rc, pszName ? pszName : "code page ", pszName ? 0UL : ulCp);
        return FALSE;
    }
    return TRUE;
}

static unsigned long ProcessCp(void)
{
    ULONG aCp[8], cb = 0;
    if (DosQueryCp(sizeof(aCp), aCp, &cb) == NO_ERROR && cb >= sizeof(ULONG))
        return aCp[0];
    return 850;                    /* a sane OS/2 default if the query fails */
}

/*--------------------------------------------------------------------------
 * Detection - the part OS/2 does not do for you
 *------------------------------------------------------------------------*/

/* Strict UTF-8 validation. Rejects overlong forms and surrogates, because
 * accepting them means mis-detecting some 8-bit text as UTF-8. */
static BOOL IsValidUtf8(const unsigned char *p, ULONG cb)
{
    ULONG i = 0;
    BOOL  bAnyMultibyte = FALSE;

    while (i < cb) {
        unsigned char c = p[i];
        int n;
        unsigned long v;

        if (c < 0x80) { i++; continue; }
        else if ((c & 0xE0) == 0xC0) { n = 1; v = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { n = 2; v = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { n = 3; v = c & 0x07; }
        else return FALSE;                       /* 0x80-0xBF lead, or > U+10FFFF */

        if (i + n >= cb)
            return FALSE;                        /* truncated sequence */
        {
            int k;
            for (k = 1; k <= n; k++) {
                if ((p[i + k] & 0xC0) != 0x80)
                    return FALSE;
                v = (v << 6) | (p[i + k] & 0x3F);
            }
        }
        if ((n == 1 && v < 0x80) || (n == 2 && v < 0x800) || (n == 3 && v < 0x10000))
            return FALSE;                        /* overlong */
        if (v >= 0xD800 && v <= 0xDFFF)
            return FALSE;                        /* surrogate half */
        if (v > 0x10FFFF)
            return FALSE;

        bAnyMultibyte = TRUE;
        i += n + 1;
    }
    /* Pure ASCII is valid UTF-8 but is better reported as ANSI: it round-trips
     * either way, and calling it ANSI keeps a plain text file's encoding menu
     * showing what the user expects. */
    return bAnyMultibyte;
}

int EncDetect(const char *pBuf, ULONG cbBuf)
{
    const unsigned char *p = (const unsigned char *)pBuf;

    if (cbBuf >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
        return NP2ENC_UTF8SIG;
    if (cbBuf >= 2 && p[0] == 0xFF && p[1] == 0xFE)
        return NP2ENC_UCS2LE;
    if (cbBuf >= 2 && p[0] == 0xFE && p[1] == 0xFF)
        return NP2ENC_UCS2BE;
    if (cbBuf > 0 && IsValidUtf8(p, cbBuf))
        return NP2ENC_UTF8;
    return NP2ENC_ANSI;
}

/*--------------------------------------------------------------------------
 * Conversion
 *------------------------------------------------------------------------*/

static void SwapUcs(UniChar *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        p[i] = (UniChar)((p[i] >> 8) | (p[i] << 8));
}

/* code-page bytes -> UCS-2. Caller frees *ppUcs. */
static BOOL ToUcs(UconvObject obj, const char *pIn, ULONG cbIn,
                  UniChar **ppUcs, size_t *pcUcs, char *pszErr, int cchErr)
{
    UniChar *pUcs;
    void    *pInCur;
    UniChar *pOutCur;
    size_t   cbLeft, cOutLeft, cSubst = 0;
    int      rc;

    /* One UniChar per input byte is always enough: no code page produces more
     * characters than it has bytes. */
    pUcs = (UniChar *)malloc((cbIn + 1) * sizeof(UniChar));
    if (!pUcs) {
        if (pszErr) sprintf(pszErr, "out of memory");
        return FALSE;
    }

    /* Pass COPIES - these get advanced past the data consumed. */
    pInCur   = (void *)pIn;
    pOutCur  = pUcs;
    cbLeft   = cbIn;
    cOutLeft = cbIn + 1;

    rc = UniUconvToUcs(obj, &pInCur, &cbLeft, &pOutCur, &cOutLeft, &cSubst);
    if (rc != 0 && rc != UCONV_EILSEQ) {
        if (pszErr) sprintf(pszErr, "UniUconvToUcs rc=%d", rc);
        free(pUcs);
        return FALSE;
    }
    *pcUcs = (size_t)(pOutCur - pUcs);
    *ppUcs = pUcs;
    return TRUE;
}

/* UCS-2 -> code-page bytes. Caller frees *ppOut. */
static BOOL FromUcs(UconvObject obj, UniChar *pUcs, size_t cUcs,
                    char **ppOut, ULONG *pcbOut, char *pszErr, int cchErr)
{
    char    *pOut;
    UniChar *pInCur;
    void    *pOutCur;
    size_t   cInLeft, cbOutLeft, cSubst = 0;
    size_t   cbCap = cUcs * 4 + 4;      /* UTF-8 worst case is 4 bytes/char */
    int      rc;

    pOut = (char *)malloc(cbCap);
    if (!pOut) {
        if (pszErr) sprintf(pszErr, "out of memory");
        return FALSE;
    }

    pInCur    = pUcs;
    pOutCur   = pOut;
    cInLeft   = cUcs;
    cbOutLeft = cbCap;

    rc = UniUconvFromUcs(obj, &pInCur, &cInLeft, &pOutCur, &cbOutLeft, &cSubst);
    if (rc != 0 && rc != UCONV_EILSEQ) {
        if (pszErr) sprintf(pszErr, "UniUconvFromUcs rc=%d", rc);
        free(pOut);
        return FALSE;
    }
    *pcbOut = (ULONG)((char *)pOutCur - pOut);
    *ppOut  = pOut;
    return TRUE;
}

BOOL EncToUtf8(int iEnc, const char *pIn, ULONG cbIn,
               char **ppOut, ULONG *pcbOut, char *pszErr, int cchErr)
{
    UconvObject objIn = NULL, objUtf8 = NULL;
    UniChar *pUcs = NULL;
    size_t   cUcs = 0;
    BOOL     bOK = FALSE;

    if (pszErr) pszErr[0] = '\0';

    /* Skip the byte-order mark: it is a signature, not content. */
    if (iEnc == NP2ENC_UTF8SIG && cbIn >= 3) { pIn += 3; cbIn -= 3; }
    if ((iEnc == NP2ENC_UCS2LE || iEnc == NP2ENC_UCS2BE) && cbIn >= 2) { pIn += 2; cbIn -= 2; }

    /* Already UTF-8: nothing to convert, just copy. */
    if (iEnc == NP2ENC_UTF8 || iEnc == NP2ENC_UTF8SIG) {
        char *p = (char *)malloc(cbIn + 1);
        if (!p) { if (pszErr) sprintf(pszErr, "out of memory"); return FALSE; }
        memcpy(p, pIn, cbIn);
        p[cbIn] = '\0';
        *ppOut = p; *pcbOut = cbIn;
        return TRUE;
    }

    if (iEnc == NP2ENC_UCS2LE || iEnc == NP2ENC_UCS2BE) {
        /* The file is already UCS-2; only endianness and the trip to UTF-8
         * remain. UCS_2 is native-endian, so a BE file is byte-swapped first. */
        cUcs = cbIn / sizeof(UniChar);
        pUcs = (UniChar *)malloc((cUcs + 1) * sizeof(UniChar));
        if (!pUcs) { if (pszErr) sprintf(pszErr, "out of memory"); return FALSE; }
        memcpy(pUcs, pIn, cUcs * sizeof(UniChar));
        if (iEnc == NP2ENC_UCS2BE)
            SwapUcs(pUcs, cUcs);
    } else {
        unsigned long ulCp = (iEnc == NP2ENC_OEM) ? 850 : ProcessCp();
        if (!OpenConv(&objIn, ulCp, NULL, pszErr, cchErr))
            return FALSE;
        if (!ToUcs(objIn, pIn, cbIn, &pUcs, &cUcs, pszErr, cchErr)) {
            UniFreeUconvObject(objIn);
            return FALSE;
        }
        UniFreeUconvObject(objIn);
    }

    if (OpenConv(&objUtf8, 0, "IBM-1208", pszErr, cchErr)) {
        bOK = FromUcs(objUtf8, pUcs, cUcs, ppOut, pcbOut, pszErr, cchErr);
        UniFreeUconvObject(objUtf8);
    }
    free(pUcs);
    return bOK;
}

BOOL EncFromUtf8(int iEnc, const char *pIn, ULONG cbIn,
                 char **ppOut, ULONG *pcbOut, char *pszErr, int cchErr)
{
    UconvObject objUtf8 = NULL, objOut = NULL;
    UniChar *pUcs = NULL;
    size_t   cUcs = 0;
    char    *pBody = NULL;
    ULONG    cbBody = 0;
    BOOL     bOK = FALSE;

    if (pszErr) pszErr[0] = '\0';

    /* Plain UTF-8 out: copy, adding the signature if that is the encoding. */
    if (iEnc == NP2ENC_UTF8 || iEnc == NP2ENC_UTF8SIG) {
        ULONG cbSig = (iEnc == NP2ENC_UTF8SIG) ? 3 : 0;
        char *p = (char *)malloc(cbIn + cbSig + 1);
        if (!p) { if (pszErr) sprintf(pszErr, "out of memory"); return FALSE; }
        if (cbSig) { p[0] = (char)0xEF; p[1] = (char)0xBB; p[2] = (char)0xBF; }
        memcpy(p + cbSig, pIn, cbIn);
        *ppOut = p; *pcbOut = cbIn + cbSig;
        return TRUE;
    }

    if (!OpenConv(&objUtf8, 0, "IBM-1208", pszErr, cchErr))
        return FALSE;
    if (!ToUcs(objUtf8, pIn, cbIn, &pUcs, &cUcs, pszErr, cchErr)) {
        UniFreeUconvObject(objUtf8);
        return FALSE;
    }
    UniFreeUconvObject(objUtf8);

    if (iEnc == NP2ENC_UCS2LE || iEnc == NP2ENC_UCS2BE) {
        ULONG cbOut = (ULONG)(cUcs * sizeof(UniChar)) + 2;
        char *p = (char *)malloc(cbOut);
        if (!p) { if (pszErr) sprintf(pszErr, "out of memory"); free(pUcs); return FALSE; }
        if (iEnc == NP2ENC_UCS2BE) {
            SwapUcs(pUcs, cUcs);
            p[0] = (char)0xFE; p[1] = (char)0xFF;
        } else {
            p[0] = (char)0xFF; p[1] = (char)0xFE;
        }
        memcpy(p + 2, pUcs, cUcs * sizeof(UniChar));
        *ppOut = p; *pcbOut = cbOut;
        free(pUcs);
        return TRUE;
    }

    {
        unsigned long ulCp = (iEnc == NP2ENC_OEM) ? 850 : ProcessCp();
        if (OpenConv(&objOut, ulCp, NULL, pszErr, cchErr)) {
            bOK = FromUcs(objOut, pUcs, cUcs, &pBody, &cbBody, pszErr, cchErr);
            UniFreeUconvObject(objOut);
        }
    }
    free(pUcs);
    if (bOK) { *ppOut = pBody; *pcbOut = cbBody; }
    return bOK;
}
