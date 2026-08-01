/* np2ini.h - a small text .ini reader/writer for the app's own settings.
 *
 * This is deliberately NOT the Prf* API. Prf* is for OS2.INI / OS2SYS.INI and
 * files opened with PrfOpenProfile - the registry equivalent, an opaque binary
 * database. An application's own config file "is its own business and may
 * perfectly well be text" [os2ref/profiles-ini.md], and Notepad2's is text, so
 * the faithful port keeps it text and uses ordinary file I/O.
 *
 * Reading loads the whole file once; writing rebuilds it from scratch, which
 * is what Notepad2 does too and avoids any in-place rewrite logic.
 */
#ifndef NP2INI_H
#define NP2INI_H

#define INCL_DOS
#include <os2.h>

/* Where the settings file lives. OS/2 is single-seat and has no per-user
 * application-data directory, so the file sits beside the .EXE
 * [recipes/porting-a-windows-app.md 7.1]. pszArgv0 may be NULL. */
void IniResolvePath(const char *pszArgv0, char *pszOut, int cchOut);

BOOL IniLoad(const char *pszPath);
void IniFree(void);

int  IniGetInt(const char *pszSection, const char *pszKey, int iDefault);
void IniGetStr(const char *pszSection, const char *pszKey, const char *pszDefault,
               char *pszOut, int cchOut);

BOOL IniBeginWrite(const char *pszPath);
void IniWriteSection(const char *pszSection);
void IniWriteInt(const char *pszKey, int iValue);
void IniWriteStr(const char *pszKey, const char *pszValue);
BOOL IniEndWrite(void);

#endif /* NP2INI_H */
