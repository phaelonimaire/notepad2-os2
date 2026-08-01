/* np2browse.h - a file browser on WC_CONTAINER.
 *
 * This is the OS/2 answer to Notepad2's Dlapi.c, which drives a Win32 shell
 * listview through IShellFolder. The pieces map:
 *
 *   ListView / TreeView       -> one WC_CONTAINER in the view you want
 *   IShellFolder::EnumObjects -> DosFindFirst / DosFindNext for a plain
 *                                directory listing (the WPS route,
 *                                _wpQueryContent, is for shell objects rather
 *                                than files on disk)
 *   SHGetFileInfo icon        -> WinLoadFileIcon
 *   ImageList                 -> nothing needed; a record carries its HPOINTER
 */
#ifndef NP2BROWSE_H
#define NP2BROWSE_H

#define INCL_WIN
#define INCL_DOS
#include <os2.h>

/* Browse starting in pszDir (updated to the directory the user ended in).
 * On OK, pszPick receives the fully-qualified file chosen. */
BOOL BrowseDlg(HWND hwndOwner, char *pszDir, int cchDir, char *pszPick, int cchPick);

#endif /* NP2BROWSE_H */
