/* np2edit.h - Notepad2's buffer operations, converted from src/Edit.c.
 *
 * These are the operations behind the Edit and Lines dialogs. They are almost
 * entirely portable C over Scintilla messages; see np2edit.c for the three places
 * where the platform actually forced a change.
 */
#ifndef NP2EDIT_H
#define NP2EDIT_H

#define INCL_WIN
#include <os2.h>

/* From Notepad2's src/Edit.h, unchanged. */
#define ALIGN_LEFT       0
#define ALIGN_RIGHT      1
#define ALIGN_CENTER     2
#define ALIGN_JUSTIFY    3
#define ALIGN_JUSTIFY_EX 4

#define SORT_ASCENDING   0
#define SORT_DESCENDING  1
#define SORT_SHUFFLE     2
#define SORT_MERGEDUP    4
#define SORT_UNIQDUP     8
#define SORT_UNIQUNIQ   16
#define SORT_NOCASE     32
#define SORT_LOGICAL    64
#define SORT_COLUMN    128

void EditJumpTo(HWND hwndEdit, LONG iNewLine, LONG iNewCol);
void EditEncloseSelection(HWND hwndEdit, const char *pszOpen, const char *pszClose);
void EditModifyLines(HWND hwndEdit, const char *pszPrefix, const char *pszAppend);
void EditAlignText(HWND hwndEdit, int nMode);
void EditSortLines(HWND hwndEdit, int iSortFlags);

#endif /* NP2EDIT_H */
