/* np2find.h - Find / Replace, converted from Notepad2's Edit.c.
 *
 * The search itself is Scintilla's (SCI_FINDTEXT / SCI_SEARCHINTARGET /
 * SCI_REPLACETARGET), so this file is the *dialog* and the *policy* around it -
 * exactly the split the Win32 original uses. Nothing here re-implements searching.
 */
#ifndef NP2FIND_H
#define NP2FIND_H

#define INCL_WIN
#include <os2.h>

#define NP2_FINDTEXT_MAX 512

/* Notepad2's EDITFINDREPLACE, minus the Win32-only members (hwnd/hInstance were
 * used for the modeless-dialog plumbing, which PM does differently). */
typedef struct _EDITFINDREPLACE {
    char  szFind[NP2_FINDTEXT_MAX];
    char  szReplace[NP2_FINDTEXT_MAX];
    ULONG fuFlags;          /* SCFIND_MATCHCASE | SCFIND_WHOLEWORD | ... */
    BOOL  bTransformBS;
    BOOL  bNoFindWrap;
} EDITFINDREPLACE;

/* The four operations. Each returns TRUE if it did something. */
BOOL EditFindNext(HWND hwndEdit, EDITFINDREPLACE *lpefr, BOOL fExtendSelection);
BOOL EditFindPrev(HWND hwndEdit, EDITFINDREPLACE *lpefr, BOOL fExtendSelection);
BOOL EditReplace(HWND hwndEdit, EDITFINDREPLACE *lpefr);
BOOL EditReplaceAll(HWND hwndEdit, EDITFINDREPLACE *lpefr, BOOL bInSelection);

/* Show the modeless Find (bReplace=FALSE) or Replace (bReplace=TRUE) dialog.
 * Only one exists at a time; calling again re-targets and re-surfaces it. */
void EditFindReplaceDlg(HWND hwndOwner, HWND hwndEdit,
                        EDITFINDREPLACE *lpefr, BOOL bReplace);

/* TRUE while the modeless dialog owns the given message - see np2.c's loop. */
HWND EditFindReplaceHwnd(void);

void EditSelectEx(HWND hwndEdit, LONG iAnchorPos, LONG iCurrentPos);

#endif /* NP2FIND_H */
