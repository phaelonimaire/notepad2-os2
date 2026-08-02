/* np2watch.h - file change notification.
 *
 * This is the one feature in Notepad2 with no OS/2 counterpart at all: there is
 * no DosFindFirstChangeNotification, no directory-change semaphore, nothing.
 * The port therefore polls.
 *
 * That is a smaller change than it sounds, because *Notepad2 already polls*.
 * FindFirstChangeNotification only tells it that something in the directory
 * moved; the decision about whether the open file actually changed is made by
 * re-reading the file's timestamp and size and comparing them
 * (Notepad2.c, WatchTimerProc). Win32 supplies a cheap gate in front of a
 * comparison that would work without it. Dropping the gate costs one
 * DosQueryPathInfo per tick against one file - not per file in the directory -
 * which at the stock two-second interval is not a measurable cost.
 *
 *   SetTimer(NULL, ID_WATCHTIMER, ...)  -> WinStartTimer + WM_TIMER on the client
 *   FindFirstFile -> ftLastWriteTime    -> DosQueryPathInfo(FIL_STANDARD)
 *   CompareFileTime, nFileSizeLow       -> fdate/ftimeLastWrite, cbFile
 *   PathFileExists                      -> DosQueryPathInfo succeeding
 *   GetTickCount                        -> WinGetCurrentTime
 *   SetForegroundWindow                 -> WinSetActiveWindow(HWND_DESKTOP, ...)
 *
 * The modes and their numbering are Notepad2's, so an .ini written by either
 * program means the same thing to the other.
 */
#ifndef NP2WATCH_H
#define NP2WATCH_H

#define INCL_WIN
#define INCL_DOS
#define INCL_DOSERRORS   /* NO_ERROR - INCL_DOS does not imply it */
#include <os2.h>

#define FILEWATCH_NONE        0   /* do not watch                              */
#define FILEWATCH_MSGBOX      1   /* ask before reloading                      */
#define FILEWATCH_AUTORELOAD  2   /* reload silently when unmodified           */

/* Notepad2's ID_WATCHTIMER. PM timer ids are per-window, so the value is free
 * to be anything; keeping it recognisable costs nothing. */
#define IDT_WATCH  0xA000

/* Posted to the client when the watched file has changed on disk. */
#define WM_CHANGENOTIFY  (WM_USER + 40)

int  FileWatchMode(void);
BOOL FileWatchResetOnNewFile(void);
void FileWatchSetOptions(int iMode, BOOL bResetOnNewFile);

/* Start, restart or stop watching. Pass NULL or "" to stop; the timer is only
 * running while a file is actually being watched. Re-arms after every reload,
 * which is also how the stamp gets updated. */
void FileWatchInstall(HWND hwndClient, const char *pszFile);

/* Call after loading a document. When a *different* file is opened, the "reset"
 * option turns watching off again, so that watching stays a deliberate act per
 * file rather than something a new document silently inherits. A reload is not
 * a new file. [Notepad2.c FileLoad: if (!bReload && bResetFileWatching)] */
void FileWatchOnNewFile(HWND hwndClient, const char *pszFile, BOOL bReload);

/* Call from WM_TIMER when SHORT1FROMMP(mp1) == IDT_WATCH. Posts
 * WM_CHANGENOTIFY to hwndClient when it decides the file has changed. */
void FileWatchTick(HWND hwndClient);

BOOL FileWatchRunning(void);

/* The File Change Notification dialog - three modes plus the reset option. */
BOOL ChangeNotifyDlg(HWND hwndOwner);

#endif /* NP2WATCH_H */
