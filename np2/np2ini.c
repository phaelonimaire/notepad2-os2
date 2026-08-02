/* np2ini.c - text .ini settings, read whole / write whole.
 *
 * Ordinary DosOpen/DosRead/DosWrite [os2ref/file-io.md], not Prf*: see the
 * header for why. Every APIRET is checked, and a missing settings file is a
 * normal first-run condition rather than an error.
 */
#define INCL_DOS
#define INCL_DOSFILEMGR
#define INCL_DOSERRORS
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "np2ini.h"

static char *pIniText = NULL;      /* whole file, NUL terminated */
static HFILE hIniWrite = NULLHANDLE;

/*--------------------------------------------------------------------------
 * Path
 *------------------------------------------------------------------------*/

void IniResolvePath(const char *pszArgv0, char *pszOut, int cchOut)
{
    const char *pSep = NULL, *p;

    pszOut[0] = '\0';

    if (pszArgv0 && pszArgv0[0]) {
        for (p = pszArgv0; *p; p++)
            if (*p == '\\' || *p == '/' || *p == ':')
                pSep = p;
    }

    if (pSep) {
        int cch = (int)(pSep - pszArgv0) + 1;
        if (cch > cchOut - 16)
            cch = cchOut - 16;
        memcpy(pszOut, pszArgv0, cch);
        pszOut[cch] = '\0';
    }
    strncat(pszOut, "np2.ini", cchOut - (int)strlen(pszOut) - 1);
}

/*--------------------------------------------------------------------------
 * Reading
 *------------------------------------------------------------------------*/

BOOL IniLoad(const char *pszPath)
{
    HFILE  hf = NULLHANDLE;
    ULONG  ulAction = 0, cbRead = 0;
    FILESTATUS3 fs3;

    IniFree();

    if (DosOpen((PSZ)pszPath, &hf, &ulAction, 0, FILE_NORMAL,
                OPEN_ACTION_OPEN_IF_EXISTS,
                OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, NULL) != NO_ERROR)
        return FALSE;                       /* first run - not an error */

    if (DosQueryFileInfo(hf, FIL_STANDARD, &fs3, sizeof(fs3)) != NO_ERROR) {
        DosClose(hf);
        return FALSE;
    }

    pIniText = (char *)malloc(fs3.cbFile + 1);
    if (!pIniText) {
        DosClose(hf);
        return FALSE;
    }
    if (DosRead(hf, pIniText, fs3.cbFile, &cbRead) != NO_ERROR) {
        DosClose(hf);
        free(pIniText);
        pIniText = NULL;
        return FALSE;
    }
    DosClose(hf);
    pIniText[cbRead] = '\0';
    return TRUE;
}

void IniFree(void)
{
    free(pIniText);
    pIniText = NULL;
}

/* Compare a line's leading token against psz, ignoring case and stopping at
 * chStop. Returns a pointer past chStop on match, NULL otherwise. */
static const char *MatchKey(const char *pLine, const char *pszKey, char chStop)
{
    while (*pszKey) {
        if (tolower((unsigned char)*pLine) != tolower((unsigned char)*pszKey))
            return NULL;
        pLine++; pszKey++;
    }
    while (*pLine == ' ' || *pLine == '\t')
        pLine++;
    return (*pLine == chStop) ? pLine + 1 : NULL;
}

/* Find the value text for section/key, or NULL. */
static const char *IniFind(const char *pszSection, const char *pszKey)
{
    const char *p = pIniText;
    BOOL bInSection = FALSE;

    if (!p)
        return NULL;

    while (*p) {
        const char *pLine = p;
        const char *pVal;

        while (*p && *p != '\r' && *p != '\n')
            p++;
        while (*p == '\r' || *p == '\n')
            p++;

        while (*pLine == ' ' || *pLine == '\t')
            pLine++;

        if (*pLine == '[') {
            const char *pName = pLine + 1;
            size_t cch = strlen(pszSection);
            bInSection = (strncasecmp(pName, pszSection, cch) == 0 &&
                          pName[cch] == ']');
            continue;
        }
        if (!bInSection || *pLine == ';' || *pLine == '#' || *pLine == '\0')
            continue;

        pVal = MatchKey(pLine, pszKey, '=');
        if (pVal) {
            while (*pVal == ' ' || *pVal == '\t')
                pVal++;
            return pVal;
        }
    }
    return NULL;
}

int IniGetInt(const char *pszSection, const char *pszKey, int iDefault)
{
    const char *pVal = IniFind(pszSection, pszKey);
    if (!pVal)
        return iDefault;
    /* strtol so a malformed value falls back rather than yielding 0 - a
     * settings file edited by hand should not silently zero things. */
    {
        char *pEnd = NULL;
        long v = strtol(pVal, &pEnd, 10);
        if (pEnd == pVal)
            return iDefault;
        return (int)v;
    }
}

void IniGetStr(const char *pszSection, const char *pszKey, const char *pszDefault,
               char *pszOut, int cchOut)
{
    const char *pVal = IniFind(pszSection, pszKey);
    int i = 0;

    if (!pVal) {
        strncpy(pszOut, pszDefault ? pszDefault : "", cchOut - 1);
        pszOut[cchOut - 1] = '\0';
        return;
    }
    while (pVal[i] && pVal[i] != '\r' && pVal[i] != '\n' && i < cchOut - 1) {
        pszOut[i] = pVal[i];
        i++;
    }
    /* Trailing blanks are almost always accidental. */
    while (i > 0 && (pszOut[i - 1] == ' ' || pszOut[i - 1] == '\t'))
        i--;
    pszOut[i] = '\0';
}

/*--------------------------------------------------------------------------
 * Writing
 *------------------------------------------------------------------------*/

static void IniPut(const char *psz)
{
    ULONG cbWritten = 0;
    if (hIniWrite != NULLHANDLE)
        DosWrite(hIniWrite, (PVOID)psz, (ULONG)strlen(psz), &cbWritten);
}

BOOL IniBeginWrite(const char *pszPath)
{
    ULONG ulAction = 0;
    if (DosOpen((PSZ)pszPath, &hIniWrite, &ulAction, 0, FILE_NORMAL,
                OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_REPLACE_IF_EXISTS,
                OPEN_ACCESS_READWRITE | OPEN_SHARE_DENYWRITE, NULL) != NO_ERROR) {
        hIniWrite = NULLHANDLE;
        return FALSE;
    }
    IniPut("; Notepad2 for OS/2 - settings\r\n"
           "; Written automatically; edit while the editor is closed.\r\n");
    return TRUE;
}

/* These take arbitrary caller strings - the longest today is a saved find/replace
 * pattern at NP2_FINDTEXT_MAX (512), which sz[600] happens to survive but only
 * just. Bounded so that a future longer value truncates the settings file rather
 * than corrupting the stack. */
void IniWriteSection(const char *pszSection)
{
    char sz[128];
    snprintf(sz, sizeof(sz), "\r\n[%s]\r\n", pszSection);
    IniPut(sz);
}

void IniWriteInt(const char *pszKey, int iValue)
{
    char sz[160];
    snprintf(sz, sizeof(sz), "%s=%d\r\n", pszKey, iValue);
    IniPut(sz);
}

void IniWriteStr(const char *pszKey, const char *pszValue)
{
    char sz[700];
    snprintf(sz, sizeof(sz), "%s=%s\r\n", pszKey, pszValue ? pszValue : "");
    IniPut(sz);
}

BOOL IniEndWrite(void)
{
    if (hIniWrite == NULLHANDLE)
        return FALSE;
    DosClose(hIniWrite);
    hIniWrite = NULLHANDLE;
    return TRUE;
}
