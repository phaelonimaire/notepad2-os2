/* np2dlg.h - the converted Notepad2 dialogs.
 *
 * Each entry point runs one modal dialog with WinDlgBox and returns TRUE if the
 * user accepted it. Settings dialogs edit NP2SETTINGS in place; the caller then
 * calls ApplySettings to push the whole set into Scintilla.
 */
#ifndef NP2DLG_H
#define NP2DLG_H

#define INCL_WIN
#include <os2.h>

/* Every editor setting the dialogs can change. Kept as one struct so
 * ApplySettings is the single place that talks to Scintilla about them -
 * the same shape as Notepad2's own globals + UpdateSettings pair. */
typedef struct _np2settings {
    /* Tabs */
    SHORT iTabWidth;
    SHORT iIndentWidth;
    BOOL  bTabsAsSpaces;
    BOOL  bTabIndents;
    BOOL  bBackspaceUnindents;

    /* Long lines */
    BOOL  bMarkLongLines;
    SHORT iLongLinesLimit;
    BOOL  bLongLineBackground;      /* FALSE = edge line, TRUE = background tint */

    /* Word wrap */
    BOOL  fWordWrap;
    SHORT iWordWrapMode;            /* 0 = between words, 1 = between any glyphs */
    SHORT iWordWrapIndent;          /* 0..6, see np2dlg.c */
    BOOL  bShowWordWrapSymbols;
    SHORT iWordWrapSymbols;         /* packed as Notepad2 stores it: tens = after
                                     * wrap, units = before wrap */
    /* View */
    BOOL  bLineNumbers;
} NP2SETTINGS;

void SettingsDefaults(NP2SETTINGS *s);
void ApplySettings(HWND hwndEdit, const NP2SETTINGS *s);

BOOL EditGotoLineDlg(HWND hwndOwner, HWND hwndEdit);
BOOL EditTabSettingsDlg(HWND hwndOwner, NP2SETTINGS *s);
BOOL EditLongLinesDlg(HWND hwndOwner, NP2SETTINGS *s);
BOOL EditWordWrapDlg(HWND hwndOwner, NP2SETTINGS *s);

BOOL EditModifyLinesDlg(HWND hwndOwner, char *pszPrefix, char *pszAppend, int cch);
BOOL EditEncloseSelectionDlg(HWND hwndOwner, char *pszOpen, char *pszClose, int cch);
BOOL EditInsertTagDlg(HWND hwndOwner, char *pszOpen, char *pszClose, int cch);
BOOL EditAlignDlg(HWND hwndOwner, int *piAlignMode);
BOOL EditSortDlg(HWND hwndOwner, HWND hwndEdit, int *piSortFlags);
void EditAboutDlg(HWND hwndOwner);

#endif /* NP2DLG_H */
