#define IDD_NP2MAIN     256

/* --- menu command ids --- */
#define IDM_FILE        100
#define IDM_NEW         101
#define IDM_EXIT        102
#define IDM_OPEN        103
#define IDM_SAVEAS      104
#define IDM_SAVE        105

#define IDM_EDIT        200
#define IDM_UNDO        201
#define IDM_REDO        202
#define IDM_CUT         203
#define IDM_COPY        204
#define IDM_PASTE       205
#define IDM_SELECTALL   206
#define IDM_LINES       207
#define IDM_MODIFYLINES 208
#define IDM_ALIGNLINES  209
#define IDM_SORTLINES   210
#define IDM_ENCLOSESEL  211
#define IDM_INSERTTAG   212

#define IDM_VIEW        300
#define IDM_WORDWRAP    301
#define IDM_LINENUMBERS 302
#define IDM_COLWRAP     303

#define IDM_SEARCH      400
#define IDM_FIND        401
#define IDM_FINDNEXT    402
#define IDM_FINDPREV    403
#define IDM_REPLACE     404
#define IDM_GOTOLINE    405

#define IDM_SETTINGS      500
#define IDM_TABSETTINGS   501
#define IDM_LONGLINESET   502
#define IDM_WORDWRAPSET   503

#define IDM_HELP        550
#define IDM_ABOUT       551

/* --- dialog template ids --- */
#define IDD_COLUMNWRAP  520
#define IDD_FIND        521
#define IDD_REPLACE     522
#define IDD_GOTOLINE    523
#define IDD_TABSETTINGS 524
#define IDD_LONGLINES   525
#define IDD_WORDWRAP    526
#define IDD_MODIFYLINES 527
#define IDD_ENCLOSESEL  528
#define IDD_INSERTTAG   529
#define IDD_ALIGN       530
#define IDD_SORT        531
#define IDD_ABOUT       532

/* --- control ids (scoped to their own dialog, so reuse across dialogs is fine) --- */

/* Find / Replace */
#define IDC_FINDTEXT        600
#define IDC_REPLACETEXT     601
#define IDC_FINDCASE        602
#define IDC_FINDWORD        603
#define IDC_FINDSTART       604
#define IDC_FINDREGEXP      605
#define IDC_FINDTRANSFORMBS 606
#define IDC_NOWRAP          607
#define IDC_FINDPREVBTN     608
#define IDC_REPLACEBTN      609
#define IDC_REPLACEALL      610
#define IDC_REPLACEINSEL    611

/* Goto */
#define IDC_LINENUM         700
#define IDC_COLNUM          701
#define IDC_COLWRAPCOL      702

/* Tab settings */
#define IDC_TABWIDTH        710
#define IDC_INDENTWIDTH     711
#define IDC_TABSASSPACES    712
#define IDC_TABINDENTS      713
#define IDC_BSUNINDENTS     714

/* Long lines */
#define IDC_LONGLIMIT       720
#define IDC_LONGEDGELINE    721
#define IDC_LONGEDGEBACK    722

/* Word wrap settings */
#define IDC_WWINDENT        730
#define IDC_WWBEFORE        731
#define IDC_WWAFTER         732
#define IDC_WWMODE          733

/* Modify Lines / Enclose Selection / Insert Tag share one dialog procedure */
#define IDC_STR1            740
#define IDC_STR2            741

/* Align - these MUST stay consecutive and in ALIGN_* order; the dialog maps
   (id - IDC_ALIGNLEFT) straight to the ALIGN_* constant. */
#define IDC_ALIGNLEFT       750
#define IDC_ALIGNRIGHT      751
#define IDC_ALIGNCENTER     752
#define IDC_ALIGNJUSTIFY    753
#define IDC_ALIGNJUSTIFYEX  754

/* Sort */
#define IDC_SORTASC         760
#define IDC_SORTDESC        761
#define IDC_SORTSHUFFLE     762
#define IDC_SORTMERGEDUP    763
#define IDC_SORTUNIQDUP     764
#define IDC_SORTUNIQUNIQ    765
#define IDC_SORTNOCASE      766
#define IDC_SORTLOGICAL     767
#define IDC_SORTCOLUMN      768

/* ==========================================================================
 * Batch 3 commands.  Grouped by submenu; values are arbitrary but kept dense
 * so the passthrough table in np2.c stays easy to scan.
 * ========================================================================== */

/* File */
#define IDM_REVERT          110
#define IDM_READONLY        111

/* Edit */
#define IDM_COPYALL         220
#define IDM_COPYADD         221
#define IDM_CLEAR           222
#define IDM_CLEARCLIPBOARD  223

/* Edit > Lines */
#define IDM_MOVELINEUP      230
#define IDM_MOVELINEDOWN    231
#define IDM_DUPLICATELINE   232
#define IDM_CUTLINE         233
#define IDM_COPYLINE        234
#define IDM_DELETELINE      235
#define IDM_SPLITLINES      236
#define IDM_JOINLINES       237
#define IDM_JOINPARAGRAPHS  238

/* Edit > Block */
#define IDM_INDENT          240
#define IDM_UNINDENT        241
#define IDM_SELDUPLICATE    242
#define IDM_PADWITHSPACES   243
#define IDM_STRIP1STCHAR    244
#define IDM_STRIPLASTCHAR   245
#define IDM_TRIMLINES       246
#define IDM_COMPRESSWS      247
#define IDM_MERGEBLANKLINES 248
#define IDM_REMOVEBLANKLINES 249

/* Edit > Enclose Selection */
#define IDM_ENCLOSE_PAREN   250
#define IDM_ENCLOSE_BRACE   251
#define IDM_ENCLOSE_BRACKET 252
#define IDM_ENCLOSE_SQUOTE  253
#define IDM_ENCLOSE_DQUOTE  254
#define IDM_ENCLOSE_BACKTICK 255

/* Edit > Convert */
#define IDM_UPPERCASE       260
#define IDM_LOWERCASE       261
#define IDM_INVERTCASE      262
#define IDM_TITLECASE       263
#define IDM_SENTENCECASE    264
#define IDM_TABIFYSEL       265
#define IDM_UNTABIFYSEL     266
#define IDM_TABIFYINDENT    267
#define IDM_UNTABIFYINDENT  268

/* Edit > Insert */
#define IDM_INSERT_TIMESHORT 270
#define IDM_INSERT_TIMELONG  271
#define IDM_INSERT_FILENAME  272
#define IDM_INSERT_PATHNAME  273

/* Edit > Special */
#define IDM_LINECOMMENT     280
#define IDM_STREAMCOMMENT   281
#define IDM_URLENCODE       282
#define IDM_URLDECODE       283
#define IDM_ESCAPECCHARS    284
#define IDM_UNESCAPECCHARS  285
#define IDM_CHAR2HEX        286
#define IDM_HEX2CHAR        287
#define IDM_FINDMATCHBRACE  288
#define IDM_SELTOMATCHBRACE 289
#define IDM_DELLINELEFT     290
#define IDM_DELLINERIGHT    291
#define IDM_DELWORDLEFT     292
#define IDM_DELWORDRIGHT    293

/* Edit > Bookmarks */
#define IDM_BOOKMARKTOGGLE  294
#define IDM_BOOKMARKNEXT    295
#define IDM_BOOKMARKPREV    296
#define IDM_BOOKMARKCLEAR   297

/* submenu anchors */
#define IDM_BLOCK           298
#define IDM_ENCLOSEMENU     299
#define IDM_CONVERT         310
#define IDM_INSERTMENU      311
#define IDM_SPECIAL         312
#define IDM_BOOKMARKS       313

/* View */
#define IDM_LONGLINEMARKER  320
#define IDM_INDENTGUIDES    321
#define IDM_SHOWWHITESPACE  322
#define IDM_SHOWEOLS        323
#define IDM_HILITECURLINE   324
#define IDM_SELMARGIN       325
#define IDM_FOLDING         326
#define IDM_TOGGLEFOLDS     327
#define IDM_ZOOMIN          328
#define IDM_ZOOMOUT         329
#define IDM_RESETZOOM       330

/* Settings */
#define IDM_TABSASSPACES    340
#define IDM_AUTOINDENT      341

/* Line endings */
#define IDM_EOL_CRLF        350
#define IDM_EOL_LF          351
#define IDM_EOL_CR          352
#define IDM_EOL_MENU        353

/* Mark Occurrences */
#define IDM_MARKOCC_MENU    360
#define IDM_MARKOCC_OFF     361
#define IDM_MARKOCC_RED     362
#define IDM_MARKOCC_GREEN   363
#define IDM_MARKOCC_BLUE    364
#define IDM_MARKOCC_CASE    365
#define IDM_MARKOCC_WORD    366

/* Reusable info box with "don't display again" */
#define IDD_INFOBOX         540
#define IDC_INFOTEXT        780
#define IDC_INFOSUPPRESS    781

/* Syntax schemes.  IDM_SCHEME_BASE..+31 are generated at run time from the
   table in np2style.c, so the menu never drifts from the schemes. */
#define IDM_SCHEME_MENU     370
#define IDM_VIEW_FONT       371
#define IDM_SCHEME_BASE     900

/* Launch */
#define IDM_LAUNCH_MENU     380
#define IDM_NEWWINDOW       381
#define IDM_EMPTYWINDOW     382
#define IDM_EXECDOC         383
#define IDM_RUNCMD          384
#define IDD_RUN             545
#define IDC_RUNCMD          790

/* Encoding */
#define IDM_ENC_MENU        390
#define IDM_ENC_ANSI        391
#define IDM_ENC_OEM         392
#define IDM_ENC_UTF8        393
#define IDM_ENC_UTF8SIG     394
#define IDM_ENC_UCS2LE      395
#define IDM_ENC_UCS2BE      396
#define IDM_RELOAD_MENU     397
/* The six IDM_RELOAD_* ids MUST stay contiguous and in NP2ENC_* order: the
 * handler maps (id - IDM_RELOAD_ANSI) straight to the encoding constant, the
 * same trick the Align dialog uses for its radio buttons. */
#define IDM_RELOAD_ANSI     460
#define IDM_RELOAD_OEM      461
#define IDM_RELOAD_UTF8     462
#define IDM_RELOAD_UTF8SIG  463
#define IDM_RELOAD_UCS2LE   464
#define IDM_RELOAD_UCS2BE   465

/* Statusbar */
#define IDM_STATUSBAR       410
#define IDC_STATUS_POS      800
#define IDC_STATUS_SEL      801
#define IDC_STATUS_ENC      802
#define IDC_STATUS_MODE     803

/* Scheme editor */
#define IDM_SCHEMECONFIG    411
#define IDD_STYLECONFIG     552
#define IDC_SLOTLIST        810
#define IDC_SLOTCOLOUR      811
#define IDC_SLOTBOLD        812
#define IDC_SLOTPREVIEW     813

/* Recent files */
#define IDM_RECENT          412
#define IDD_RECENT          555
#define IDC_RECENTLIST      820
#define IDC_RECENTREMOVE    821
#define IDC_RECENTCLEAR     822

/* File browser (WC_CONTAINER) */
#define IDM_BROWSE          413
#define IDD_BROWSE          560
#define IDC_BROWSECNR       830
#define IDC_BROWSEPATH      831

/* File change notification. The three IDC_WATCH_* mode buttons MUST stay
 * contiguous and in FILEWATCH_* order - np2watch.c indexes them as
 * IDC_WATCH_NONE + mode. */
#define IDM_CHANGENOTIFY    433
#define IDM_CONTEXTMENU     434

/* Search / edit commands from Notepad2's long tail */
#define IDM_SELTONEXT       436
#define IDM_SELTOPREV       437
#define IDM_REPLACENEXT     438
#define IDM_SAVEFIND        439
#define IDM_SWAPCLIP        440
#define IDM_COMPLETEWORD    441
#define IDM_SAVECOPY        442
#define IDM_SAVESETTINGS    443
#define IDM_SAVERECENT      444
#define IDM_SAVEFINDREPL    445
#define IDM_STICKYWINPOS    446
#define IDM_AUTOCOMPWORDS   447
#define IDM_PROPERTIES      448
#define IDM_TEXTEXCERPT     449
#define IDM_REUSEWINDOW     450
#define IDM_SINGLEFILEINST  451
#define IDM_COLUMNWRAP      452

/* A second instance hands its filename to the first through the system atom
 * table; mp1 carries the ATOM. See np2run.c. */
#define NP2_OPENFILE (WM_USER + 50)
/* Sent (not posted) to ask an instance whether mp1's atom names the file it has
 * open. Answers TRUE/FALSE; the asker owns the atom. */
#define NP2_QUERYFILE (WM_USER + 51)
#define IDD_CHANGENOTIFY    565
#define IDC_WATCH_NONE      840
#define IDC_WATCH_MSGBOX    841
#define IDC_WATCH_AUTO      842
#define IDC_WATCH_RESET     843

/* Printing */
#define IDM_PRINT           414
#define IDM_PAGESETUP       415

/* Favorites / desktop link / open with */
#define IDM_FAVORITES       416
#define IDM_ADDTOFAV        417
#define IDM_CREATELINK      418
#define IDM_OPENWITH        419

/* Window title format + Esc key + misc preferences */
#define IDM_TITLE_MENU      420
#define IDM_TITLE_NAMEONLY  421
#define IDM_TITLE_NAMEDIR   422
#define IDM_TITLE_FULLPATH  423
#define IDM_ESC_MENU        424
#define IDM_ESC_NONE        425
#define IDM_ESC_MINIMIZE    426
#define IDM_ESC_EXIT        427
#define IDM_ALWAYSONTOP     428
#define IDM_AUTOCLOSETAGS   429
#define IDM_SAVESETTINGSNOW 430
#define IDM_OPENINIFILE     431

/* Toolbar */
#define IDM_TOOLBAR         432
