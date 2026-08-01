/* np2print.h - printing through the queued print DC.
 *
 * Win32's PrintDlg splits on OS/2 into two things: DevPostDeviceModes for the
 * job-properties dialog (the driver's own), and DevOpenDC(OD_QUEUED) plus the
 * DEVESC_STARTDOC / DEVESC_ENDDOC bracket for the job itself.
 * See os2ref/printing-spooler.md.
 */
#ifndef NP2PRINT_H
#define NP2PRINT_H

#define INCL_WIN
#include <os2.h>

BOOL PrintDocument(HWND hwndOwner, HWND hwndEdit, const char *pszTitle,
                   const char *pszFontFace, int iFontSize);
BOOL PrintSetup(HWND hwndOwner);

#endif /* NP2PRINT_H */
