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
#define IDC_COLUMNWRAP  100

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
