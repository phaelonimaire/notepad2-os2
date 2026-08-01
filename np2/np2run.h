/* np2run.h - Notepad2's Launch menu: new window, execute document, run command.
 *
 * Win32 uses ShellExecuteEx for all of these. OS/2 splits the job:
 *
 *   run a program          -> DosStartSession   (os2ref/session-manager.md)
 *   open a document with
 *   its associated program -> WinQueryObject + WinOpenObject, i.e. ask the
 *                             Workplace Shell to open it, which is what
 *                             "associated application" means on OS/2
 */
#ifndef NP2RUN_H
#define NP2RUN_H

#define INCL_WIN
#define INCL_DOS
#include <os2.h>

/* Start a program in its own session. bPM selects a PM session rather than a
 * windowed text session. Reports failure rather than returning silently. */
BOOL RunProgram(HWND hwndOwner, const char *pszPgm, const char *pszArgs, BOOL bPM);

/* Hand a file to the Workplace Shell to open however it is associated. */
BOOL RunOpenDocument(HWND hwndOwner, const char *pszFile);

/* Notepad2's Run dialog: a command line, executed via RunProgram. */
BOOL RunCommandDlg(HWND hwndOwner, char *pszCmd, int cchCmd);

#endif /* NP2RUN_H */
