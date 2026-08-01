/* np2enc.h - encoding detection and conversion.
 *
 * Model: the document is held inside Scintilla as UTF-8 (SCI_SETCODEPAGE
 * SC_CP_UTF8) whatever the file was, and converted on the way in and out.
 * That is Notepad2's own model, and it is what makes search, sort and case
 * conversion behave the same regardless of the file's encoding.
 *
 * Conversion goes through UCS-2, which is OS/2's internal Unicode form:
 *
 *     file bytes --UniUconvToUcs--> UCS-2 --UniUconvFromUcs--> UTF-8 bytes
 *
 * Detection is NOT part of that API - OS/2 converts from a code set you name,
 * it does not guess one [os2ref/unicode-conversion.md 9.2]. The BOM sniff and
 * the UTF-8 well-formedness check below are the hand-written part.
 */
#ifndef NP2ENC_H
#define NP2ENC_H

#define INCL_DOS
#include <os2.h>

enum {
    NP2ENC_ANSI = 0,     /* the process code page */
    NP2ENC_OEM,          /* code page 850 */
    NP2ENC_UTF8,
    NP2ENC_UTF8SIG,      /* UTF-8 with a BOM */
    NP2ENC_UCS2LE,
    NP2ENC_UCS2BE,
    NP2ENC_COUNT
};

const char *EncName(int iEnc);

/* Sniff a buffer: BOM first, then UTF-8 well-formedness, else ANSI. */
int  EncDetect(const char *pBuf, ULONG cbBuf);

/* Convert file bytes -> UTF-8 for the editor. Caller frees *ppOut.
 * Returns FALSE and leaves *pszErr describing the failure. */
BOOL EncToUtf8(int iEnc, const char *pIn, ULONG cbIn,
               char **ppOut, ULONG *pcbOut, char *pszErr, int cchErr);

/* Convert the editor's UTF-8 back out to the target encoding. */
BOOL EncFromUtf8(int iEnc, const char *pIn, ULONG cbIn,
                 char **ppOut, ULONG *pcbOut, char *pszErr, int cchErr);

#endif /* NP2ENC_H */
