/* np2style.h - syntax schemes, the useful core of Notepad2's Styles.c.
 *
 * Notepad2's Styles.c is 5,169 lines, most of which is a scheme *editor* and a
 * per-style .ini format. What actually makes an editor highlight code is much
 * smaller: pick a lexer from the file extension, hand it its keywords, and map
 * that lexer's style numbers onto colours. That is what this file does.
 *
 * The scheme editor (IDD_STYLECONFIG / IDD_STYLESELECT) is deliberately not
 * here - see NEXT.md. Schemes are compiled in rather than read from a file,
 * because settings persistence is not written yet; making them editable but
 * unsaveable would be the worse half of the feature.
 */
#ifndef NP2STYLE_H
#define NP2STYLE_H

#define INCL_WIN
#include <os2.h>

int         Style_Count(void);
const char *Style_Name(int iScheme);

/* Match a scheme by file extension; returns 0 (plain text) when nothing fits. */
int  Style_MatchFromFile(const char *pszFile);

/* Apply a scheme to the control: lexer, keywords, colours, fold properties. */
void Style_Apply(HWND hwndEdit, int iScheme, const char *pszFontFace, int iFontSize);

/* TRUE if the scheme's lexer produces fold levels, so Code Folding is real. */
BOOL Style_SupportsFolding(int iScheme);

/* WinFontDlg -> the editor's default font. Returns TRUE if the user chose one. */
BOOL Style_ChooseFont(HWND hwndOwner, char *pszFace, int cchFace, int *piSize);

#endif /* NP2STYLE_H */
