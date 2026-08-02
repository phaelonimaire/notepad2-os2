/* np2cmd.h - Notepad2's Block / Convert / Insert / Special text transforms,
 * converted from src/Edit.c.
 *
 * Everything here operates on the selection (or the whole document when there is
 * no selection, where Notepad2 does that) and is wrapped in a single undo action.
 */
#ifndef NP2CMD_H
#define NP2CMD_H

#define INCL_WIN
#include <os2.h>

/* Convert */
void EditInvertCase(HWND h);
void EditTitleCase(HWND h);
void EditSentenceCase(HWND h);
void EditTabsToSpaces(HWND h, int iTabWidth, BOOL bOnlyIndent);
void EditSpacesToTabs(HWND h, int iTabWidth, BOOL bOnlyIndent);

/* Block */
void EditStripFirstCharacter(HWND h);
void EditStripLastCharacter(HWND h);
void EditStripTrailingBlanks(HWND h);
void EditCompressSpaces(HWND h);
void EditRemoveBlankLines(HWND h, BOOL bMerge);
void EditPadWithSpaces(HWND h);

/* Lines */
void EditSplitLines(HWND h);
void EditJoinLines(HWND h, BOOL bParagraph);

/* Special */
void EditURLEncode(HWND h);
void EditURLDecode(HWND h);
void EditEscapeCChars(HWND h);
void EditUnescapeCChars(HWND h);
void EditChar2Hex(HWND h);
void EditHex2Char(HWND h);
void EditToggleLineComments(HWND h, const char *pszComment, BOOL bInsertAtStart);

/* Clipboard swap, word selection, and completion from words in the document. */
void EditSwapClipboard(HWND h);
void EditSelectWord(HWND h);
LONG EditGetSelOrWord(HWND h, char *pszOut, LONG cchOut);
BOOL EditCompleteWord(HWND h);
void EditGetExcerpt(HWND h, char *pszOut, int cchOut);
void EditFindMatchingBrace(HWND h, BOOL bSelect);

/* Insert */
void EditInsertDateTime(HWND h, BOOL bShort);
void EditInsertString(HWND h, const char *psz);

/* Clipboard */
void EditCopyAppend(HWND h);

/* Mark Occurrences - highlight every copy of the selected word.
 * iMark: 0 = off, 1 = red, 2 = green, 3 = blue (Notepad2's numbering). */
void EditMarkAll(HWND h, int iMark, BOOL bMatchCase, BOOL bMatchWords);

#endif /* NP2CMD_H */
