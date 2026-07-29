/* np2style.c - syntax schemes.
 *
 * Structure: every lexer numbers its own styles differently (SCE_C_COMMENT is 1,
 * SCE_PL_COMMENTLINE is 2, SCE_BAT_COMMENT is 1 ...), so rather than repeat a
 * colour per lexer, each scheme carries a small map from *that lexer's* style
 * number to a shared semantic slot. Add a language by adding one table, not by
 * touching any drawing code.
 *
 * Colours are Scintilla's 0xBBGGRR, which is NOT the order a Win32 COLORREF
 * literal reads in and is the reverse of what GpiSetColor takes in RGB mode.
 * They are written here as named constants for that reason.
 */
#define INCL_WIN
#define INCL_GPI
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "Scintilla.h"
#include "SciLexer.h"
#include "np2style.h"

static LONG SciMsg(HWND h, unsigned int msg, LONG wp, const void *lp)
{
    return LONGFROMMR(WinSendMsg(h, msg, MPFROMLONG(wp), MPFROMP((PVOID)lp)));
}
#define SciL(h, m, wp)      SciMsg((h), (m), (LONG)(wp), NULL)
#define SciP(h, m, wp, lp)  SciMsg((h), (m), (LONG)(wp), (const void *)(lp))

/* SCI_SETPROPERTY carries a key pointer in wParam and a value pointer in
 * lParam - neither is an integer, so it needs its own sender. */
static void SciProp(HWND h, const char *pszKey, const char *pszVal)
{
    WinSendMsg(h, SCI_SETPROPERTY, MPFROMP((PVOID)pszKey), MPFROMP((PVOID)pszVal));
}

/*--------------------------------------------------------------------------
 * Palette.  Scintilla colour = 0xBBGGRR.
 *------------------------------------------------------------------------*/

/* Named NP2C_* rather than CLR_*: os2emx.h already defines CLR_BLACK,
 * CLR_BLUE, CLR_RED, CLR_GREEN and CLR_WHITE as PM colour *indices*, which
 * are not RGB values and would silently mean something else here. */
#define NP2C_BLACK   0x000000L
#define NP2C_WHITE   0xFFFFFFL
#define NP2C_GREEN   0x008000L   /* comments      */
#define NP2C_BLUE    0xFF0000L   /* keywords      */
#define NP2C_MAROON  0x000080L   /* strings       */
#define NP2C_PURPLE  0x800080L   /* numbers       */
#define NP2C_TEAL    0x808000L   /* preprocessor  */
#define NP2C_GRAY    0x808080L   /* operators     */
#define NP2C_DKRED   0x0000C0L   /* attributes    */
#define NP2C_NAVY    0x800000L   /* tags          */
#define NP2C_OLIVE   0x008080L   /* headings      */
#define NP2C_RED     0x0000FFL   /* bad brace     */

/* Semantic slots. */
enum {
    SEM_DEFAULT = 0, SEM_COMMENT, SEM_KEYWORD, SEM_KEYWORD2, SEM_STRING,
    SEM_NUMBER, SEM_PREPROC, SEM_OPERATOR, SEM_TAG, SEM_ATTRIB, SEM_HEADING,
    SEM_COUNT
};

static const struct { LONG clr; BOOL bBold; } aSem[SEM_COUNT] = {
    { NP2C_BLACK,  FALSE },   /* DEFAULT   */
    { NP2C_GREEN,  FALSE },   /* COMMENT   */
    { NP2C_BLUE,   TRUE  },   /* KEYWORD   */
    { NP2C_TEAL,   TRUE  },   /* KEYWORD2  */
    { NP2C_MAROON, FALSE },   /* STRING    */
    { NP2C_PURPLE, FALSE },   /* NUMBER    */
    { NP2C_TEAL,   FALSE },   /* PREPROC   */
    { NP2C_GRAY,   FALSE },   /* OPERATOR  */
    { NP2C_NAVY,   TRUE  },   /* TAG       */
    { NP2C_DKRED,  FALSE },   /* ATTRIB    */
    { NP2C_OLIVE,  TRUE  }    /* HEADING   */
};

typedef struct { short style; short sem; } STYLEMAP;
#define MAPEND { -1, -1 }

/*--------------------------------------------------------------------------
 * Per-lexer style maps
 *------------------------------------------------------------------------*/

static const STYLEMAP mapCPP[] = {
    { SCE_C_COMMENT, SEM_COMMENT }, { SCE_C_COMMENTLINE, SEM_COMMENT },
    { SCE_C_COMMENTDOC, SEM_COMMENT }, { SCE_C_COMMENTLINEDOC, SEM_COMMENT },
    { SCE_C_NUMBER, SEM_NUMBER }, { SCE_C_WORD, SEM_KEYWORD },
    { SCE_C_WORD2, SEM_KEYWORD2 },
    { SCE_C_STRING, SEM_STRING }, { SCE_C_CHARACTER, SEM_STRING },
    { SCE_C_VERBATIM, SEM_STRING }, { SCE_C_STRINGRAW, SEM_STRING },
    { SCE_C_PREPROCESSOR, SEM_PREPROC }, { SCE_C_OPERATOR, SEM_OPERATOR },
    { SCE_C_REGEX, SEM_STRING },
    MAPEND
};

static const STYLEMAP mapHTML[] = {
    { SCE_H_TAG, SEM_TAG }, { SCE_H_TAGUNKNOWN, SEM_TAG },
    { SCE_H_TAGEND, SEM_TAG }, { SCE_H_XMLSTART, SEM_TAG },
    { SCE_H_XMLEND, SEM_TAG },
    { SCE_H_ATTRIBUTE, SEM_ATTRIB }, { SCE_H_ATTRIBUTEUNKNOWN, SEM_ATTRIB },
    { SCE_H_NUMBER, SEM_NUMBER },
    { SCE_H_DOUBLESTRING, SEM_STRING }, { SCE_H_SINGLESTRING, SEM_STRING },
    { SCE_H_VALUE, SEM_STRING },
    { SCE_H_COMMENT, SEM_COMMENT }, { SCE_H_XCCOMMENT, SEM_COMMENT },
    { SCE_H_ENTITY, SEM_PREPROC }, { SCE_H_CDATA, SEM_PREPROC },
    { SCE_H_QUESTION, SEM_PREPROC },
    MAPEND
};

static const STYLEMAP mapPython[] = {
    { SCE_P_COMMENTLINE, SEM_COMMENT }, { SCE_P_COMMENTBLOCK, SEM_COMMENT },
    { SCE_P_NUMBER, SEM_NUMBER }, { SCE_P_WORD, SEM_KEYWORD },
    { SCE_P_STRING, SEM_STRING }, { SCE_P_CHARACTER, SEM_STRING },
    { SCE_P_TRIPLE, SEM_STRING }, { SCE_P_TRIPLEDOUBLE, SEM_STRING },
    { SCE_P_CLASSNAME, SEM_TAG }, { SCE_P_DEFNAME, SEM_TAG },
    { SCE_P_OPERATOR, SEM_OPERATOR },
    MAPEND
};

static const STYLEMAP mapPerl[] = {
    { SCE_PL_COMMENTLINE, SEM_COMMENT }, { SCE_PL_POD, SEM_COMMENT },
    { SCE_PL_NUMBER, SEM_NUMBER }, { SCE_PL_WORD, SEM_KEYWORD },
    { SCE_PL_STRING, SEM_STRING }, { SCE_PL_CHARACTER, SEM_STRING },
    { SCE_PL_PREPROCESSOR, SEM_PREPROC }, { SCE_PL_OPERATOR, SEM_OPERATOR },
    { SCE_PL_SCALAR, SEM_ATTRIB }, { SCE_PL_ARRAY, SEM_ATTRIB },
    MAPEND
};

static const STYLEMAP mapBash[] = {
    { SCE_SH_COMMENTLINE, SEM_COMMENT }, { SCE_SH_NUMBER, SEM_NUMBER },
    { SCE_SH_WORD, SEM_KEYWORD }, { SCE_SH_STRING, SEM_STRING },
    { SCE_SH_CHARACTER, SEM_STRING }, { SCE_SH_OPERATOR, SEM_OPERATOR },
    { SCE_SH_SCALAR, SEM_ATTRIB }, { SCE_SH_PARAM, SEM_ATTRIB },
    { SCE_SH_BACKTICKS, SEM_PREPROC },
    MAPEND
};

static const STYLEMAP mapBatch[] = {
    { SCE_BAT_COMMENT, SEM_COMMENT }, { SCE_BAT_WORD, SEM_KEYWORD },
    { SCE_BAT_LABEL, SEM_TAG }, { SCE_BAT_HIDE, SEM_PREPROC },
    { SCE_BAT_COMMAND, SEM_KEYWORD2 }, { SCE_BAT_IDENTIFIER, SEM_ATTRIB },
    { SCE_BAT_OPERATOR, SEM_OPERATOR },
    MAPEND
};

static const STYLEMAP mapSQL[] = {
    { SCE_C_COMMENT, SEM_COMMENT }, { SCE_C_COMMENTLINE, SEM_COMMENT },
    { SCE_C_NUMBER, SEM_NUMBER }, { SCE_C_WORD, SEM_KEYWORD },
    { SCE_C_STRING, SEM_STRING }, { SCE_C_CHARACTER, SEM_STRING },
    { SCE_C_OPERATOR, SEM_OPERATOR },
    MAPEND
};

static const STYLEMAP mapMake[] = {
    { SCE_MAKE_COMMENT, SEM_COMMENT }, { SCE_MAKE_PREPROCESSOR, SEM_PREPROC },
    { SCE_MAKE_IDENTIFIER, SEM_ATTRIB }, { SCE_MAKE_OPERATOR, SEM_OPERATOR },
    { SCE_MAKE_TARGET, SEM_TAG },
    MAPEND
};

static const STYLEMAP mapProps[] = {
    { SCE_PROPS_COMMENT, SEM_COMMENT }, { SCE_PROPS_SECTION, SEM_TAG },
    { SCE_PROPS_ASSIGNMENT, SEM_OPERATOR }, { SCE_PROPS_DEFVAL, SEM_STRING },
    { SCE_PROPS_KEY, SEM_KEYWORD },
    MAPEND
};

static const STYLEMAP mapDiff[] = {
    { SCE_DIFF_COMMENT, SEM_COMMENT }, { SCE_DIFF_COMMAND, SEM_KEYWORD },
    { SCE_DIFF_HEADER, SEM_HEADING }, { SCE_DIFF_POSITION, SEM_PREPROC },
    { SCE_DIFF_DELETED, SEM_STRING }, { SCE_DIFF_ADDED, SEM_TAG },
    { SCE_DIFF_CHANGED, SEM_ATTRIB },
    MAPEND
};

static const STYLEMAP mapPascal[] = {
    { SCE_PAS_COMMENT, SEM_COMMENT }, { SCE_PAS_COMMENT2, SEM_COMMENT },
    { SCE_PAS_COMMENTLINE, SEM_COMMENT },
    { SCE_PAS_PREPROCESSOR, SEM_PREPROC }, { SCE_PAS_PREPROCESSOR2, SEM_PREPROC },
    { SCE_PAS_NUMBER, SEM_NUMBER }, { SCE_PAS_HEXNUMBER, SEM_NUMBER },
    { SCE_PAS_WORD, SEM_KEYWORD }, { SCE_PAS_STRING, SEM_STRING },
    { SCE_PAS_CHARACTER, SEM_STRING }, { SCE_PAS_OPERATOR, SEM_OPERATOR },
    MAPEND
};

static const STYLEMAP mapLua[] = {
    { SCE_LUA_COMMENT, SEM_COMMENT }, { SCE_LUA_COMMENTLINE, SEM_COMMENT },
    { SCE_LUA_COMMENTDOC, SEM_COMMENT }, { SCE_LUA_NUMBER, SEM_NUMBER },
    { SCE_LUA_WORD, SEM_KEYWORD }, { SCE_LUA_WORD2, SEM_KEYWORD2 },
    { SCE_LUA_STRING, SEM_STRING }, { SCE_LUA_CHARACTER, SEM_STRING },
    { SCE_LUA_LITERALSTRING, SEM_STRING },
    { SCE_LUA_PREPROCESSOR, SEM_PREPROC }, { SCE_LUA_OPERATOR, SEM_OPERATOR },
    MAPEND
};

static const STYLEMAP mapAsm[] = {
    { SCE_ASM_COMMENT, SEM_COMMENT }, { SCE_ASM_COMMENTBLOCK, SEM_COMMENT },
    { SCE_ASM_NUMBER, SEM_NUMBER }, { SCE_ASM_STRING, SEM_STRING },
    { SCE_ASM_CHARACTER, SEM_STRING }, { SCE_ASM_OPERATOR, SEM_OPERATOR },
    { SCE_ASM_CPUINSTRUCTION, SEM_KEYWORD },
    { SCE_ASM_MATHINSTRUCTION, SEM_KEYWORD },
    { SCE_ASM_REGISTER, SEM_ATTRIB }, { SCE_ASM_DIRECTIVE, SEM_PREPROC },
    { SCE_ASM_DIRECTIVEOPERAND, SEM_KEYWORD2 },
    MAPEND
};

static const STYLEMAP mapCSS[] = {
    { SCE_CSS_COMMENT, SEM_COMMENT }, { SCE_CSS_TAG, SEM_TAG },
    { SCE_CSS_CLASS, SEM_KEYWORD }, { SCE_CSS_PSEUDOCLASS, SEM_KEYWORD2 },
    { SCE_CSS_ID, SEM_TAG }, { SCE_CSS_IDENTIFIER, SEM_ATTRIB },
    { SCE_CSS_VALUE, SEM_STRING }, { SCE_CSS_OPERATOR, SEM_OPERATOR },
    { SCE_CSS_DOUBLESTRING, SEM_STRING }, { SCE_CSS_SINGLESTRING, SEM_STRING },
    { SCE_CSS_IMPORTANT, SEM_PREPROC }, { SCE_CSS_DIRECTIVE, SEM_PREPROC },
    MAPEND
};

static const STYLEMAP mapVB[] = {
    { SCE_B_COMMENT, SEM_COMMENT }, { SCE_B_NUMBER, SEM_NUMBER },
    { SCE_B_KEYWORD, SEM_KEYWORD }, { SCE_B_KEYWORD2, SEM_KEYWORD2 },
    { SCE_B_STRING, SEM_STRING }, { SCE_B_PREPROCESSOR, SEM_PREPROC },
    { SCE_B_OPERATOR, SEM_OPERATOR }, { SCE_B_CONSTANT, SEM_ATTRIB },
    MAPEND
};

static const STYLEMAP mapNone[] = { MAPEND };

/*--------------------------------------------------------------------------
 * Keyword sets
 *------------------------------------------------------------------------*/

static const char kwC[] =
    "auto break case char const continue default do double else enum extern "
    "float for goto if inline int long register restrict return short signed "
    "sizeof static struct switch typedef union unsigned void volatile while "
    "class namespace template typename public private protected virtual "
    "friend operator new delete this throw try catch bool true false nullptr "
    "explicit mutable using constexpr override final noexcept static_cast "
    "dynamic_cast const_cast reinterpret_cast";

static const char kwJS[] =
    "break case catch class const continue debugger default delete do else "
    "export extends finally for function if import in instanceof let new "
    "return super switch this throw try typeof var void while with yield "
    "null true false async await of";

static const char kwPython[] =
    "and as assert async await break class continue def del elif else except "
    "finally for from global if import in is lambda nonlocal not or pass "
    "raise return try while with yield True False None self";

static const char kwPerl[] =
    "if elsif else unless while until for foreach do sub return my our local "
    "use no require package BEGIN END last next redo goto and or not eq ne lt "
    "gt le ge cmp print printf sprintf push pop shift unshift split join "
    "defined undef ref bless die warn open close chomp chop";

static const char kwBash[] =
    "if then else elif fi case esac for while until do done in function "
    "select time return break continue local export readonly declare unset "
    "shift eval exec exit trap set source alias";

static const char kwBatch[] =
    "call do else errorlevel exist for goto if in not set setlocal endlocal "
    "shift echo off on pause rem start defined equ neq lss leq gtr geq";

static const char kwSQL[] =
    "select from where insert update delete create drop alter table view "
    "index into values set and or not null is like between in exists group "
    "by having order asc desc join inner left right outer on union all "
    "distinct as case when then else end begin commit rollback primary key "
    "foreign references default check unique constraint";

static const char kwPascal[] =
    "and array asm begin case const constructor destructor div do downto "
    "else end file for function goto if implementation in inherited "
    "interface label mod nil not object of or packed procedure program "
    "record repeat set shl shr string then to type unit until uses var "
    "while with xor";

static const char kwLua[] =
    "and break do else elseif end false for function goto if in local nil "
    "not or repeat return then true until while";

static const char kwVB[] =
    "and as boolean byref byte byval call case class const continue dim do "
    "double each else elseif end enum error exit false for function get "
    "goto if implements in integer is let long loop me mod new next not "
    "nothing object on option optional or preserve private property public "
    "redim rem resume return select set single static step stop string sub "
    "then to true type until variant wend while with xor";

static const char kwCSS[] =
    "color background background-color background-image border margin "
    "padding font font-family font-size font-weight display position top "
    "left right bottom width height float clear text-align line-height "
    "overflow z-index opacity content cursor visibility white-space";

static const char kwHTML[] =
    "a abbr address area article aside audio b base blockquote body br "
    "button canvas caption cite code col colgroup dd del details dfn div dl "
    "dt em embed fieldset figcaption figure footer form h1 h2 h3 h4 h5 h6 "
    "head header hr html i iframe img input ins kbd label legend li link "
    "main map mark meta nav noscript object ol optgroup option output p "
    "param pre progress q s samp script section select small source span "
    "strong style sub summary sup table tbody td textarea tfoot th thead "
    "time title tr track u ul var video wbr";

static const char kwAsm[] =
    "mov push pop lea add sub mul imul div idiv inc dec cmp test and or xor "
    "not neg shl shr sal sar jmp je jne jz jnz jg jge jl jle ja jae jb jbe "
    "call ret int iret nop hlt loop enter leave";

/*--------------------------------------------------------------------------
 * The schemes
 *------------------------------------------------------------------------*/

typedef struct {
    const char     *pszName;
    const char     *pszExts;     /* lower case, space separated, no dots */
    int             iLexer;
    const char     *pszKw;
    const char     *pszKw2;
    const STYLEMAP *pMap;
    BOOL            bFold;
} NP2SCHEME;

static const NP2SCHEME aSchemes[] = {
 { "Text",         "txt log nfo diz me 1st readme", SCLEX_NULL,   NULL,    NULL, mapNone,   FALSE },
 { "C / C++",      "c cpp cxx cc h hpp hxx inl",    SCLEX_CPP,    kwC,     NULL, mapCPP,    TRUE  },
 { "JavaScript",   "js json jsx mjs",               SCLEX_CPP,    kwJS,    NULL, mapCPP,    TRUE  },
 { "Java",         "java",                          SCLEX_CPP,    kwJS,    NULL, mapCPP,    TRUE  },
 { "HTML",         "html htm xhtml shtml",          SCLEX_HTML,   kwHTML,  NULL, mapHTML,   TRUE  },
 { "XML",          "xml xsl xslt xsd dtd svg rss wsdl plist", SCLEX_XML, NULL, NULL, mapHTML, TRUE },
 { "CSS",          "css",                           SCLEX_CSS,    kwCSS,   NULL, mapCSS,    TRUE  },
 { "Python",       "py pyw pyi",                    SCLEX_PYTHON, kwPython, NULL, mapPython, TRUE },
 { "Perl",         "pl pm pod cgi",                 SCLEX_PERL,   kwPerl,  NULL, mapPerl,   TRUE  },
 { "Shell Script", "sh bash zsh ksh profile",       SCLEX_BASH,   kwBash,  NULL, mapBash,   TRUE  },
 { "Batch / CMD",  "bat cmd btm",                   SCLEX_BATCH,  kwBatch, NULL, mapBatch,  FALSE },
 { "SQL",          "sql",                           SCLEX_SQL,    kwSQL,   NULL, mapSQL,    TRUE  },
 { "Makefile",     "mak mk makefile gnumakefile",   SCLEX_MAKEFILE, NULL,  NULL, mapMake,   FALSE },
 { "INI / Config", "ini cfg conf inf reg properties", SCLEX_PROPERTIES, NULL, NULL, mapProps, FALSE },
 { "Diff / Patch", "diff patch rej",                SCLEX_DIFF,   NULL,    NULL, mapDiff,   FALSE },
 { "Pascal",       "pas dpr dfm inc lpr",           SCLEX_PASCAL, kwPascal, NULL, mapPascal, TRUE },
 { "Lua",          "lua",                           SCLEX_LUA,    kwLua,   NULL, mapLua,    TRUE  },
 { "Assembler",    "asm s inc32",                   SCLEX_ASM,    kwAsm,   NULL, mapAsm,    FALSE },
 { "Visual Basic", "vb vbs bas frm cls",            SCLEX_VB,     kwVB,    NULL, mapVB,     TRUE  },
 { "Markdown",     "md markdown mdown",             SCLEX_MARKDOWN, NULL,  NULL, mapNone,   FALSE },
 { "YAML",         "yaml yml",                      SCLEX_YAML,   NULL,    NULL, mapNone,   TRUE  },
};

#define NSCHEMES ((int)(sizeof(aSchemes) / sizeof(aSchemes[0])))

int Style_Count(void) { return NSCHEMES; }

const char *Style_Name(int i)
{
    return (i >= 0 && i < NSCHEMES) ? aSchemes[i].pszName : "";
}

BOOL Style_SupportsFolding(int i)
{
    return (i >= 0 && i < NSCHEMES) ? aSchemes[i].bFold : FALSE;
}

/*--------------------------------------------------------------------------
 * Extension matching
 *------------------------------------------------------------------------*/

/* Compare one space-delimited token against psz, case-insensitively. */
static BOOL TokenEq(const char *pTok, size_t cch, const char *psz)
{
    size_t i;
    for (i = 0; i < cch; i++) {
        if (!psz[i] ||
            tolower((unsigned char)pTok[i]) != tolower((unsigned char)psz[i]))
            return FALSE;
    }
    return psz[cch] == '\0';
}

int Style_MatchFromFile(const char *pszFile)
{
    const char *pExt, *pBase, *p;
    int i;

    if (!pszFile || !pszFile[0])
        return 0;

    /* Basename first: a path may contain dots that are not extensions. */
    pBase = pszFile;
    for (p = pszFile; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            pBase = p + 1;

    pExt = strrchr(pBase, '.');
    /* "makefile" has no extension - match the whole name in that case. */
    pExt = pExt ? pExt + 1 : pBase;

    for (i = 1; i < NSCHEMES; i++) {          /* 0 is Text, the fallback */
        const char *pList = aSchemes[i].pszExts;
        while (*pList) {
            const char *pTok = pList;
            while (*pList && *pList != ' ')
                pList++;
            if (TokenEq(pTok, (size_t)(pList - pTok), pExt))
                return i;
            while (*pList == ' ')
                pList++;
        }
    }
    return 0;
}

/*--------------------------------------------------------------------------
 * Applying a scheme
 *------------------------------------------------------------------------*/

void Style_Apply(HWND hwndEdit, int iScheme, const char *pszFontFace, int iFontSize)
{
    const NP2SCHEME *ps;
    const STYLEMAP  *pm;

    if (iScheme < 0 || iScheme >= NSCHEMES)
        iScheme = 0;
    ps = &aSchemes[iScheme];

    /* Base style first, then STYLECLEARALL so every style inherits the font -
     * doing it the other way round leaves the syntax styles on the old font
     * and only the plain text changes, which looks like a half-applied font. */
    SciP(hwndEdit, SCI_STYLESETFONT, STYLE_DEFAULT, pszFontFace);
    SciMsg(hwndEdit, SCI_STYLESETSIZE, STYLE_DEFAULT, (const void *)(LONG)iFontSize);
    SciMsg(hwndEdit, SCI_STYLESETFORE, STYLE_DEFAULT, (const void *)NP2C_BLACK);
    SciMsg(hwndEdit, SCI_STYLESETBACK, STYLE_DEFAULT, (const void *)NP2C_WHITE);
    SciL(hwndEdit, SCI_STYLECLEARALL, 0);

    SciL(hwndEdit, SCI_SETLEXER, ps->iLexer);
    SciP(hwndEdit, SCI_SETKEYWORDS, 0, ps->pszKw  ? ps->pszKw  : "");
    SciP(hwndEdit, SCI_SETKEYWORDS, 1, ps->pszKw2 ? ps->pszKw2 : "");

    for (pm = ps->pMap; pm->style >= 0; pm++) {
        SciMsg(hwndEdit, SCI_STYLESETFORE, pm->style,
               (const void *)aSem[pm->sem].clr);
        if (aSem[pm->sem].bBold)
            SciMsg(hwndEdit, SCI_STYLESETBOLD, pm->style, (const void *)1L);
    }

    /* Brace matching wants a visible pair of styles regardless of language. */
    SciMsg(hwndEdit, SCI_STYLESETFORE, STYLE_BRACELIGHT, (const void *)NP2C_BLUE);
    SciMsg(hwndEdit, SCI_STYLESETBOLD, STYLE_BRACELIGHT, (const void *)1L);
    SciMsg(hwndEdit, SCI_STYLESETFORE, STYLE_BRACEBAD,   (const void *)NP2C_RED);
    SciMsg(hwndEdit, SCI_STYLESETBOLD, STYLE_BRACEBAD,   (const void *)1L);

    /* Fold levels only exist if the lexer emits them; setting the property on
     * a lexer that ignores it is harmless but pointless. */
    SciProp(hwndEdit, "fold", ps->bFold ? "1" : "0");
    if (ps->bFold) {
        SciProp(hwndEdit, "fold.compact", "0");
        SciProp(hwndEdit, "fold.comment", "1");
        SciProp(hwndEdit, "fold.preprocessor", "1");
    }

    /* Colourise the WHOLE document: SCI_COLOURISE(start, end) with end = -1
     * means "to the end". Passing 0 for lParam - which is what a generic
     * "send with no lParam" helper does - asks it to colourise the range
     * [0,0), i.e. nothing, and the file renders entirely in the default
     * style with no error anywhere. */
    SciMsg(hwndEdit, SCI_COLOURISE, 0, (const void *)-1L);
}

/*--------------------------------------------------------------------------
 * Font selection - WinFontDlg
 *
 * FONTDLG is input and output like FILEDLG, and like FILEDLG it needs cbSize
 * set before the call [os2ref/resources-and-dialogs.md 10.3]. It also needs a
 * presentation space to measure with, and a caller-supplied family-name
 * buffer with its length in usFamilyBufLen - leaving that zero yields a
 * dialog that opens and returns nothing.
 *------------------------------------------------------------------------*/

BOOL Style_ChooseFont(HWND hwndOwner, char *pszFace, int cchFace, int *piSize)
{
    FONTDLG fd;
    CHAR    szFamily[FACESIZE];
    HPS     hps;
    BOOL    bOK = FALSE;

    memset(&fd, 0, sizeof(fd));
    memset(szFamily, 0, sizeof(szFamily));
    strncpy(szFamily, pszFace, sizeof(szFamily) - 1);

    hps = WinGetPS(hwndOwner);

    fd.cbSize         = sizeof(FONTDLG);
    fd.hpsScreen      = hps;
    fd.pszTitle       = (PSZ)"Default Font";
    fd.pszFamilyname  = (PSZ)szFamily;
    fd.usFamilyBufLen = sizeof(szFamily);
    fd.fxPointSize    = MAKEFIXED(*piSize, 0);
    fd.fl             = FNTS_CENTER | FNTS_INITFROMFATTRS | FNTS_FIXEDWIDTHONLY;
    fd.clrFore        = CLR_BLACK;   /* PM colour INDEX here, not RGB */
    fd.clrBack        = CLR_WHITE;
    fd.usWeight       = 5;

    if (WinFontDlg(HWND_DESKTOP, hwndOwner, &fd) && fd.lReturn == DID_OK) {
        strncpy(pszFace, (char *)fd.fAttrs.szFacename, cchFace - 1);
        pszFace[cchFace - 1] = '\0';
        if (!pszFace[0])
            strncpy(pszFace, szFamily, cchFace - 1);
        *piSize = (int)(fd.fxPointSize >> 16);
        if (*piSize < 4)
            *piSize = 10;
        bOK = TRUE;
    }

    if (hps != NULLHANDLE)
        WinReleasePS(hps);
    return bOK;
}
