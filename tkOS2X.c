/* 
 * tkOS2X.c --
 *
 *	This file contains OS/2 PM emulation procedures for X routines. 
 *
 * Copyright (c) 1996-1997 Illya Vaes
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1994 Software Research Associates, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkInt.h"
#include "tkOS2Int.h"

/*
 * The following declaration is a special purpose backdoor into the
 * Tcl notifier.  It is used to process events on the Tcl event queue,
 * without reentering the system event queue.
 */

extern void             TclOS2FlushEvents _ANSI_ARGS_((void));

/*
 * Declarations of static variables used in this file.
 */

static Display *os2Display;	/* Display that represents OS/2 PM screen. */
static Tcl_HashTable windowTable;
				/* Table of child windows indexed by handle. */
static char os2ScreenName[] = "PM:0";
                                /* Default name of OS2 display. */
static ATOM topLevelAtom, childAtom;
                                /* Atoms for the classes registered by Tk. */

/*
 * Forward declarations of procedures used in this file.
 */

static void             DeleteWindow _ANSI_ARGS_((HWND hwnd));
static void 		GetTranslatedKey (XKeyEvent *xkey);
static void 		TranslateEvent (HWND hwnd, ULONG message,
			    MPARAM param1, MPARAM param2);

/*
 *----------------------------------------------------------------------
 *
 * TkGetServerInfo --
 *
 *	Given a window, this procedure returns information about
 *	the window server for that window.  This procedure provides
 *	the guts of the "winfo server" command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkGetServerInfo(interp, tkwin)
    Tcl_Interp *interp;		/* The server information is returned in
				 * this interpreter's result. */
    Tk_Window tkwin;		/* Token for window;  this selects a
				 * particular display and server. */
{
    char buffer[50];
    ULONG info[QSV_MAX]= {0};	/* System Information Data Buffer */
    APIRET rc;
    int index = 3;
    static char* os[] = {"OS/2, non-2.x",
                         "OS/2 2.0", "OS/2 2.01", "OS/2 2.1", "OS/2 2.11",
                         "OS/2 Warp 3", "OS/2 Warp 4"};

    /* Request all available system information */
    rc= DosQuerySysInfo (1L, QSV_MAX, (PVOID)info, sizeof(info));
    switch ( info[QSV_VERSION_MAJOR] ) {
        case 20:
           switch ( info[QSV_VERSION_MINOR] ) {
               case 0:
                   index= 1;
                   break;
               case 1:
                   index= 2;
                   break;
               case 10:
                   index= 3;
                   break;
               case 11:
                   index= 4;
                   break;
            }
        case 30:
           index= 5;
           break;
        case 40:
           index= 6;
           break;
        default:
           index= 0;
           break;
    }
    sprintf(buffer, "%s (revision %d)", os[index], (int)info[QSV_VERSION_REVISION]);
    Tcl_AppendResult(interp, buffer, os[index], (char *) NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2GetTkModule --
 *
 *      This function returns the module handle for the Tk DLL.
 *
 * Results:
 *      Returns the library module handle.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

HMODULE
TkOS2GetTkModule()
{
    char libName[13];
    HMODULE mod;

#ifdef DEBUG
printf("TkOS2GetTkModule\n");
#endif
    /*
    sprintf(libName, "tk%d%d.dll", TK_MAJOR_VERSION, TK_MINOR_VERSION);
    if ( DosQueryModuleHandle(libName, &mod) != NO_ERROR) {
        return NULLHANDLE;
    } else {
        return mod;
    }
    */
    return dllHandle;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2GetAppInstance --
 *
 *	Retrieves the global application instance handle.
 *
 * Results:
 *	Returns the global application instance handle.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

HAB
TkOS2GetAppInstance()
{
    return hab;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2XInit --
 *
 *	Initialize Xlib emulation layer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets up various data structures.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2XInit( hInstance )
    HAB hInstance;
{
    BOOL success;
    static initialized = 0;

    if (initialized != 0) {
        return;
    }
    initialized = 1;

    /*
     * Register the TopLevel window class.
     */
    
/*
#ifdef DEBUG
printf("Registering Toplevel\n");
#endif
*/
    success = WinRegisterClass(hab, TOC_TOPLEVEL, TkOS2TopLevelProc,
                               CS_SIZEREDRAW, sizeof(ULONG));
    if (!success) {
        panic("Unable to register TkTopLevel class");
    }

    /*
     * Register the Child window class.
     */

/*
#ifdef DEBUG
printf("Registering Child\n");
#endif
*/
    /*
     * Don't use CS_SIZEREDRAW for the child, this will make vertical resizing
     * work incorrectly (keeping the same distance from the bottom instead of
     * from the top when using Tk's "pack ... -side top").
     */
    success = WinRegisterClass(hab, TOC_CHILD, TkOS2ChildProc, 0,
                               sizeof(ULONG));
    if (!success) {
        panic("Unable to register TkChild class");
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetDefaultScreenName --
 *
 *      Returns the name of the screen that Tk should use during
 *      initialization.
 *
 * Results:
 *      Returns a statically allocated string.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
TkGetDefaultScreenName(interp, screenName)
    Tcl_Interp *interp;         /* Not used. */
    char *screenName;           /* If NULL, use default string. */
{
    char *DISPLAY = NULL;

#ifdef DEBUG
    printf("TkGetDefaultScreenName [%s] ", screenName);
#endif
    if ((screenName == NULL) || (screenName[0] == '\0')) {
        DISPLAY = getenv("DISPLAY");
        if (DISPLAY != NULL) {
            screenName = DISPLAY;
        } else {
            screenName = os2ScreenName;
        }
    }
#ifdef DEBUG
    printf("returns [%s]\n", screenName);
#endif
    return screenName;
}

/*
 *----------------------------------------------------------------------
 *
 * XOpenDisplay --
 *
 *	Create the Display structure and fill it with device
 *	specific information.
 *
 * Results:
 *	Returns a Display structure on success or NULL on failure.
 *
 * Side effects:
 *	Allocates a new Display structure.
 *
 *----------------------------------------------------------------------
 */

Display *
XOpenDisplay(display_name)
    _Xconst char *display_name;
{
    Screen *screen;
    TkOS2Drawable *todPtr;

    TkOS2PointerInit();

    Tcl_InitHashTable(&windowTable, TCL_ONE_WORD_KEYS);

    if (os2Display != NULL) {
#ifdef DEBUG
        printf("XOpenDisplay display_name [%s], os2Display->display_name [%s]\n",
               display_name, os2Display->display_name);
#endif
	if (strcmp(os2Display->display_name, display_name) == 0) {
	    return os2Display;
	} else {
	    panic("XOpenDisplay: tried to open multiple displays");
	    return NULL;
	}
    }

    os2Display = (Display *) ckalloc(sizeof(Display));
    if (!os2Display) {
        return (Display *)None;
    }
    os2Display->display_name = (char *) ckalloc(strlen(display_name)+1);
    if (!os2Display->display_name) {
	ckfree((char *)os2Display);
        return (Display *)None;
    }
    strcpy(os2Display->display_name, display_name);

    os2Display->cursor_font = 1;
    os2Display->nscreens = 1;
    os2Display->request = 1;
    os2Display->qlen = 0;

    screen = (Screen *) ckalloc(sizeof(Screen));
    if (!screen) {
	ckfree((char *)os2Display->display_name);
	ckfree((char *)os2Display);
	return (Display *)None;
    }
    screen->display = os2Display;

    screen->width = aDevCaps[CAPS_WIDTH];
    screen->height = yScreen;
    screen->mwidth = (screen->width * 1000) / aDevCaps[CAPS_HORIZONTAL_RESOLUTION];
    screen->mheight = (screen->width * 1000) / aDevCaps[CAPS_VERTICAL_RESOLUTION];

    /*
     * Set up the root window.
     */

    todPtr = (TkOS2Drawable*) ckalloc(sizeof(TkOS2Drawable));
    if (!todPtr) {
	ckfree((char *)os2Display->display_name);
	ckfree((char *)os2Display);
	ckfree((char *)screen);
	return (Display *)None;
    }
    todPtr->type = TOD_WINDOW;
    todPtr->window.winPtr = NULL;
    todPtr->window.handle = HWND_DESKTOP;
    screen->root = (Window)todPtr;

    screen->root_depth = aDevCaps[CAPS_COLOR_BITCOUNT];
    screen->root_visual = (Visual *) ckalloc(sizeof(Visual));
    if (!screen->root_visual) {
	ckfree((char *)os2Display->display_name);
	ckfree((char *)os2Display);
	ckfree((char *)screen);
	ckfree((char *)todPtr);
	return (Display *)None;
    }
    screen->root_visual->visualid = 0;
    if ( aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER ) {
	screen->root_visual->map_entries = aDevCaps[CAPS_COLOR_INDEX]+1;
	screen->root_visual->class = PseudoColor;
    } else {
	if (screen->root_depth == 4) {
	    screen->root_visual->class = StaticColor;
	    screen->root_visual->map_entries = 16;
	} else if (screen->root_depth == 16) {
	    /*
	    screen->root_visual->class = DirectColor;
	    screen->root_visual->map_entries = aDevCaps[CAPS_COLOR_INDEX]+1;
	    screen->root_visual->red_mask = 0xfb;
	    screen->root_visual->green_mask = 0xfb00;
	    screen->root_visual->blue_mask = 0xfb0000;
	    */
	    screen->root_visual->class = TrueColor;
	    screen->root_visual->map_entries = aDevCaps[CAPS_COLOR_INDEX]+1;
	    screen->root_visual->red_mask = 0xff;
	    screen->root_visual->green_mask = 0xff00;
	    screen->root_visual->blue_mask = 0xff0000;
	} else if (screen->root_depth >= 24) {
	    screen->root_visual->class = TrueColor;
	    screen->root_visual->map_entries = 256;
	    screen->root_visual->red_mask = 0xff;
	    screen->root_visual->green_mask = 0xff00;
	    screen->root_visual->blue_mask = 0xff0000;
	}
    }
    screen->root_visual->bits_per_rgb = screen->root_depth;
    /*
    ckfree ((char *)aDevCaps);
    */

    /*
     * Note that these pixel values are not palette relative.
     */

    screen->white_pixel = RGB(255, 255, 255);
    screen->black_pixel = RGB(0, 0, 0);

    os2Display->screens = screen;
    os2Display->nscreens = 1;
    os2Display->default_screen = 0;
    screen->cmap = XCreateColormap(os2Display, None, screen->root_visual,
	    AllocNone);

    return os2Display;
}

/*
 *----------------------------------------------------------------------
 *
 * XBell --
 *
 *	Generate a beep.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Plays a sounds out the system speakers.
 *
 *----------------------------------------------------------------------
 */

void
XBell(display, percent)
    Display* display;
    int percent;
{
    DosBeep (abs(percent)*0x7FFF, percent);
}


MRESULT EXPENTRY
TkOS2FrameProc(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    static inMoveSize = 0;

    if (inMoveSize) {
/*
*/
        TclOS2FlushEvents();
    }
/*
*/

    switch (message) {
        case WM_CALCVALIDRECTS: {
            RECTL *rects = (RECTL *) PVOIDFROMMP(param1);
            SWP *newpos = (SWP *) PVOIDFROMMP(param2);
#ifdef DEBUG
printf("FrameProc: WM_CALCVALIDRECTS hwnd %x, (%d,%d;%d,%d)->(%d,%d;%d,%d); %d,%d;%dx%d\n",
       hwnd, rects[0].xLeft, rects[0].yBottom, rects[0].xRight, rects[0].yTop,
       rects[1].xLeft, rects[1].yBottom, rects[1].xRight, rects[1].yTop,
       newpos->x, newpos->y, newpos->cx, newpos->cy);
#endif
return CVR_REDRAW;
            break;
	}

	case WM_CREATE: {
/*
	    CREATESTRUCT *info = (CREATESTRUCT *) PVOIDFROMMP(param2);
*/
            TkOS2Drawable *todPtr = (TkOS2Drawable *) PVOIDFROMMP(param1);
	    Tcl_HashEntry *hPtr;
	    int new;
	    BOOL rc;
#ifdef DEBUG
printf("FrameProc: WM_CREATE hwnd %x, tod %x\n", hwnd, todPtr);
#endif

	    /*
	     * Add the window and handle to the window table.
	     */

            todPtr->window.handle = hwnd;
	    hPtr = Tcl_CreateHashEntry(&windowTable, (char *)hwnd, &new);
	    if (!new) {
		panic("Duplicate window handle: %p", hwnd);
	    }
	    Tcl_SetHashValue(hPtr, todPtr);

	    /*
	     * Store the pointer to the drawable structure passed into
	     * WinCreateWindow in the user data slot of the window.
	     */

	    rc= WinSetWindowULong(hwnd, QWL_USER, (ULONG)todPtr);
            break;
	}

	case WM_DESTROY: {
#ifdef DEBUG
printf("FrameProc: WM_DESTROY hwnd %x\n", hwnd);
#endif
            DeleteWindow(hwnd);
	    return 0;
	}
	    

	case WM_QUERYTRACKINFO: {
	    TRACKINFO *track = (TRACKINFO *)PVOIDFROMMP(param2);
	    SWP pos;
	    BOOL rc;

#ifdef DEBUG
printf("FrameProc: WM_QUERYTRACKINFO hwnd %x, trackinfo %x\n", hwnd, track);
#endif
            inMoveSize = 1;
            /* Get present size as default max/min */
            rc = WinQueryWindowPos(hwnd, &pos);
#ifdef DEBUG
if (rc!=TRUE) printf("    WinQueryWindowPos ERROR %x\n", WinGetLastError(hab));
else printf("    WinQueryWindowPos OK\n");
#endif
            /* Fill in defaults */
            track->cxBorder = track->cyBorder = 4; /* 4 pixels tracking */
            track->cxGrid = track->cyGrid = 1; /* smooth tracking */
            track->cxKeyboard = track->cyKeyboard = 8; /* fast keyboardtracking */
            rc = WinSetRect(hab, &track->rclTrack, pos.x, pos.y,
                            pos.x + pos.cx, pos.y + pos.cy);
#ifdef DEBUG
if (rc!=TRUE) printf("    WinSetRect ERROR %x\n", WinGetLastError(hab));
else printf("    WinSetRect OK\n");
#endif
            rc = WinSetRect(hab, &track->rclBoundary, 0, 0, xScreen, yScreen);
#ifdef DEBUG
if (rc!=TRUE) printf("    WinSetRect ERROR %x\n", WinGetLastError(hab));
else printf("    WinSetRect OK\n");
#endif
            track->ptlMinTrackSize.x = 0;
            track->ptlMaxTrackSize.x = xScreen;
            track->ptlMinTrackSize.y = 0;
            track->ptlMaxTrackSize.y = yScreen;
            track->fs = SHORT1FROMMP(param1);
            /* Determine what Tk will allow */
            TkOS2WmSetLimits(hwnd, (TRACKINFO *) PVOIDFROMMP(param2));
            inMoveSize = 0;
            return (MRESULT)1;	/* continue sizing or moving */
	}

	case WM_REALIZEPALETTE:
	    /* Notifies that the input focus window has realized its logical
	     * palette, so realize ours and update client area(s)
	     * Must return 0
	     */
#ifdef DEBUG
printf("FrameProc: WM_REALIZEPALETTE hwnd %x\n", hwnd);
#endif
	    TkOS2WmInstallColormaps(hwnd, WM_REALIZEPALETTE, FALSE);
            break;

	case WM_SETFOCUS:
	    /* 
             * If usfocus is true we translate the event *AND* install the
             * colormap, otherwise we only translate the event.
             */
#ifdef DEBUG
printf("FrameProc: WM_SETFOCUS hwnd %x, usfocus %x\n", hwnd, param2);
#endif
	    if ( LONGFROMMP(param2) == TRUE ) {
                HPS hps = WinGetPS(hwnd);
                ULONG colorsChanged;
                TkOS2Drawable *todPtr =
                    (TkOS2Drawable *) WinQueryWindowULong(hwnd, QWL_USER);

                if (TkOS2GetWinPtr(todPtr) != NULL) {
                    GpiSelectPalette(hps,
                       TkOS2GetPalette(TkOS2GetWinPtr(todPtr)->atts.colormap));
                    WinRealizePalette(hwnd, hps, &colorsChanged);
                    /*
	            TkOS2WmInstallColormaps(hwnd, WM_SETFOCUS, FALSE);
                    */
                }
	    }
	    TranslateEvent(hwnd, message, param1, param2);
	    break;

	case WM_TRACKFRAME: {
#ifdef DEBUG
printf("FrameProc: WM_TRACKFRAME flags %x\n", (USHORT)param1);
#endif
/*
            break;
*/
	}

	case WM_ADJUSTWINDOWPOS:
            break;

	case WM_ADJUSTFRAMEPOS: {
#ifdef DEBUG
	    SWP *pos = (SWP *) PVOIDFROMMP(param1);
printf("FrameProc: WM_ADJUSTFRAMEPOS %x, pos %d,%d (%dx%d) fl %x\n", hwnd,
pos->x, pos->y, pos->cx, pos->cy, pos->fl);
#endif
            break;
	}

	case WM_OWNERPOSCHANGE:
#ifdef DEBUG
printf("FrameProc: WM_OWNERPOSCHANGE\n");
#endif
            break;

	case WM_SHOW:
#ifdef DEBUG
printf("FrameProc: WM_SHOW\n");
#endif
            break;


	case WM_SIZE:
#ifdef DEBUG
printf("FrameProc: WM_SIZE\n");
#endif
            break;

	case WM_WINDOWPOSCHANGED: {
	    SWP *pos = (SWP *) PVOIDFROMMP(param1);
	    TkOS2Drawable *todPtr = (TkOS2Drawable *)
	                            WinQueryWindowULong(hwnd, QWL_USER);
#ifdef DEBUG
printf("FrameProc: WM_WINDOWPOSCHANGED hwnd %x, x%d,y%d,w%d,h%d,fl%x, Awp%x, tod %x\n",
       hwnd, pos->x, pos->y, pos->cx, pos->cy, pos->fl, LONGFROMMP(param2), todPtr);
#endif
            TkOS2WmConfigure(TkOS2GetWinPtr(todPtr), pos);
            break;
	}

	case WM_COMMAND: {
#ifdef DEBUG
printf("FrameProc: WM_COMMAND\n");
#endif
            break;
	}

	case WM_SYSCOMMAND: {
#ifdef DEBUG
printf("FrameProc: WM_SYSCOMMAND\n");
#endif
            break;
	}

	    /*
	     * The frame sends the WM_CLOSE to the client (child) if it exists,
	     * so we have to handle it there.
	     * Also hand off mouse/button stoff to default procedure.
	     */
	case WM_CLOSE:
	case WM_BUTTON1DOWN:
	case WM_BUTTON2DOWN:
	case WM_BUTTON3DOWN:
	case WM_BUTTON1UP:
	case WM_BUTTON2UP:
	case WM_BUTTON3UP:
	case WM_MOUSEMOVE:
            break;

	case WM_CHAR:
#ifdef DEBUG
printf("FrameProc: WM_CHAR\n");
#endif
	/* NOT IN PM, in WM_CHAR
	case WM_KEYDOWN:
	case WM_KEYUP:
	*/
#ifdef DEBUG
printf("First one pertinent\n");
#endif
/* Change necessary */
            /* WM_KEYDOWN  - Cat:030 Type:020 Area:230
             * Replace with WM_CHAR message testing SHORT1FROMMP(mp1) for
             * the KC_KEYUP flag.  The CHARMSG macro and CHRMSG structure
             * can be used to reference the flags, repeat count, scancode,
             * character code, and virtual key codes.  All keyboard
             * messages in OS/2 are handled in the WM_CHAR message. Message
             * filtering on WM_KEYDOWN message must be handled by
             * installing a WH_CHECKMSGFILTER hook.
             * Replace WM_KEYUP with WM_CHAR testing KC_KEYUP flag */
#ifdef DEBUG
printf("FrameProc: TranslateEvent hwnd %x\n", hwnd);
#endif
	    TranslateEvent(hwnd, message, param1, param2);
            return 0;

	/* NOT IN PM, in WM_CHAR
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	*/
/* Change necessary */
	    /* WM_SYSKEYDOWN is equivalent to KC_ALT down but not KC_CTRL in 
	     * SHORT1FROMMP(mp1) in WM_CHAR *and* vitrual key must not be
	     * VK_CTRL or VK_F10. The CHARMSG macro and CHRMSG structure can
	     * be used to reference the flags, repeat count, scancode,
	     * character code, and virtual key codes.  All keyboard messages
	     * in OS/2 are handled in the WM_CHAR message. Message filtering
	     * on WM_SYSKEYDOWN message must be handled by installing a
	     * WH_CHECKMSGFILTER hook.
	     * WM_SYSKEYUP ditto.
	     */
	case WM_DESTROYCLIPBOARD:
#ifdef DEBUG
printf("FrameProc: WM_DESTROYCLIPBOARD hwnd %x\n", hwnd);
#endif
	    TranslateEvent(hwnd, message, param1, param2);

	    /*
	     * We need to pass these messages to the default window
	     * procedure in order to get the system menu to work.
	     */

	    break;
        default:
    }

    return oldFrameProc(hwnd, message, param1, param2);
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2TopLevelProc --
 *
 *	Callback from Presentation Manager whenever an event occurs on a
 *	top level window.
 *
 * Results:
 *	Standard OS/2 PM return value.
 *
 * Side effects:
 *	Default window behavior.
 *
 *----------------------------------------------------------------------
 */

MRESULT EXPENTRY
TkOS2TopLevelProc(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    static inMoveSize = 0;

    if (inMoveSize) {
        TclOS2FlushEvents();
    }

    switch (message) {
/* ? WM_TRACKFRAME */
/*
        case WM_ENTERSIZEMOVE:
            inMoveSize = 1;
            break;

        case WM_EXITSIZEMOVE:
            inMoveSize = 0;
            break;
*/
	case WM_CREATE: {
/*
	    CREATESTRUCT *info = (CREATESTRUCT *) PVOIDFROMMP(param2);
*/
            TkOS2Drawable *todPtr = (TkOS2Drawable *) PVOIDFROMMP(param1);
	    Tcl_HashEntry *hPtr;
	    int new;
	    BOOL rc;
#ifdef DEBUG
printf("Toplevel: WM_CREATE hwnd %x, tod %x\n", hwnd, todPtr);
#endif

	    /*
	     * Add the window and handle to the window table.
	     */

            todPtr->window.handle = hwnd;
	    hPtr = Tcl_CreateHashEntry(&windowTable, (char *)hwnd, &new);
	    if (!new) {
		panic("Duplicate window handle: %p", hwnd);
	    }
	    Tcl_SetHashValue(hPtr, todPtr);

	    /*
	     * Store the pointer to the drawable structure passed into
	     * WinCreateWindow in the user data slot of the window.
	     */

	    rc= WinSetWindowULong(hwnd, QWL_USER, (ULONG)todPtr);
	    return 0;
	}

	case WM_ERASEBACKGROUND: {
#ifdef DEBUG
	    HWND child = WinQueryWindow(hwnd, QW_TOP);
            PRECTL pRectl = PVOIDFROMMP(param2);
	    /* We get this from the frame window when that gets a WM_PAINT */
printf("Top: WM_ERASEBACKGROUND hwnd %x, child %x, xL %d, xR %d, yT %d, yB %d\n",
hwnd, child, pRectl->xLeft, pRectl->xRight, pRectl->yTop, pRectl->yBottom);
#endif
            /* Invalidate the relevant rectangle (sends WM_PAINT) */
/* DON'T do this when responding to WM_PAINT of CS_SYNCPAINT windows */
/*
            WinInvalidateRect(child, pRectl, FALSE);
*/
	    return 0;
        }

	case WM_DESTROY: {
#ifdef DEBUG
printf("Toplevel: WM_DESTROY hwnd %x\n", hwnd);
#endif
            DeleteWindow(hwnd);
	    return 0;
	}
	    
	case WM_QUERYTRACKINFO:
#ifdef DEBUG
printf("Toplevel: WM_QUERYTRACKINFO hwnd %x, trackinfo %x\n", hwnd, param2);
#endif
            TkOS2WmSetLimits(hwnd, (TRACKINFO *) PVOIDFROMMP(param2));
/*
            return 0;
*/
break;

	case WM_REALIZEPALETTE:
	    /* Notifies that the input focus window has realized its logical
	     * palette, so realize ours and update client area(s)
	     * Must return 0
	     */
#ifdef DEBUG
printf("Toplevel: WM_REALIZEPALETTE hwnd %x\n", hwnd);
#endif
	    TkOS2WmInstallColormaps(hwnd, WM_REALIZEPALETTE, FALSE);
	    return 0;

	case WM_SETFOCUS:
	    /* 
             * If usfocus is true we translate the event *AND* install the
             * colormap, otherwise we only translate the event.
             */
#ifdef DEBUG
printf("Toplevel: WM_SETFOCUS hwnd %x, usfocus %x\n", hwnd, param2);
#endif
	    if ( LONGFROMMP(param2) == TRUE ) {
                HPS hps = WinGetPS(hwnd);
                ULONG colorsChanged;
                TkOS2Drawable *todPtr =
                    (TkOS2Drawable *) WinQueryWindowULong(hwnd, QWL_USER);
#ifdef DEBUG
printf("    PS %x\n", hps);
#endif

                if (TkOS2GetWinPtr(todPtr) != NULL) {
                    GpiSelectPalette(hps,
                       TkOS2GetPalette(TkOS2GetWinPtr(todPtr)->atts.colormap));
                    WinRealizePalette(hwnd, hps, &colorsChanged);
                    /*
	            TkOS2WmInstallColormaps(hwnd, WM_SETFOCUS, FALSE);
                    */
                }
                WinReleasePS(hps);
                /* Send to TITLEBAR */
                /*
                WinSendMsg(WinWindowFromID(hwnd, FID_TITLEBAR), WM_ACTIVATE,);
                */
	    }
	    TranslateEvent(hwnd, message, param1, param2);
	    return 0;

	case WM_ADJUSTWINDOWPOS:
            return 0;

	case WM_ADJUSTFRAMEPOS:
#ifdef DEBUG
printf("Toplevel: WM_ADJUSTFRAMEPOS\n");
#endif
            return 0;

	case WM_OWNERPOSCHANGE:
#ifdef DEBUG
printf("Toplevel: WM_OWNERPOSCHANGE\n");
#endif
            return 0;

	case WM_SIZE:
#ifdef DEBUG
printf("Toplevel: WM_SIZE\n");
#endif
            return 0;

	case WM_WINDOWPOSCHANGED: {
	    SWP *pos = (SWP *) PVOIDFROMMP(param1);
	    TkOS2Drawable *todPtr =
	        (TkOS2Drawable *) WinQueryWindowULong(hwnd, QWL_USER);
#ifdef DEBUG
printf("Toplevel: WM_WINDOWPOSCHANGED hwnd %x, swp (x%d,y%d,w%d,h%d,flags %x), tod %x\n",
       hwnd, pos->x, pos->y, pos->cx, pos->cy, pos->fl, todPtr);
#endif
            TkOS2WmConfigure(TkOS2GetWinPtr(todPtr), pos);
	    return 0;
	}

	case WM_CLOSE:
	case WM_BUTTON1DOWN:
	case WM_BUTTON2DOWN:
	case WM_BUTTON3DOWN:
	case WM_BUTTON1UP:
	case WM_BUTTON2UP:
	case WM_BUTTON3UP:
	case WM_MOUSEMOVE:
	case WM_CHAR:
	/* NOT IN PM, in WM_CHAR
	case WM_KEYDOWN:
	case WM_KEYUP:
	*/
/* Change necessary */
            /* WM_KEYDOWN  - Cat:030 Type:020 Area:230
             * Replace with WM_CHAR message testing SHORT1FROMMP(mp1) for
             * the KC_KEYUP flag.  The CHARMSG macro and CHRMSG structure
             * can be used to reference the flags, repeat count, scancode,
             * character code, and virtual key codes.  All keyboard
             * messages in OS/2 are handled in the WM_CHAR message. Message
             * filtering on WM_KEYDOWN message must be handled by
             * installing a WH_CHECKMSGFILTER hook.
             * Replace WM_KEYUP with WM_CHAR testing KC_KEYUP flag */
#ifdef DEBUG
printf("Toplevel: TranslateEvent hwnd %x\n", hwnd);
#endif
	    TranslateEvent(hwnd, message, param1, param2);
	    return 0;

	/* NOT IN PM, in WM_CHAR
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	*/
/* Change necessary */
	    /* WM_SYSKEYDOWN is equivalent to KC_ALT down but not KC_CTRL in 
	     * SHORT1FROMMP(mp1) in WM_CHAR *and* vitrual key must not be
	     * VK_CTRL or VK_F10. The CHARMSG macro and CHRMSG structure can
	     * be used to reference the flags, repeat count, scancode,
	     * character code, and virtual key codes.  All keyboard messages
	     * in OS/2 are handled in the WM_CHAR message. Message filtering
	     * on WM_SYSKEYDOWN message must be handled by installing a
	     * WH_CHECKMSGFILTER hook.
	     * WM_SYSKEYUP ditto.
	     */
	case WM_DESTROYCLIPBOARD:
#ifdef DEBUG
printf("Toplevel: WM_DESTROYCLIPBOARD hwnd %x\n", hwnd);
#endif
	    TranslateEvent(hwnd, message, param1, param2);

	    /*
	     * We need to pass these messages to the default window
	     * procedure in order to get the system menu to work.
	     */

	    break;
    }

    return WinDefWindowProc(hwnd, message, param1, param2);
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2ChildProc --
 *
 *	Callback from Presentation Manager whenever an event occurs on
 *	a child window.
 *
 * Results:
 *	Standard OS/2 PM return value.
 *
 * Side effects:
 *	Default window behavior.
 *
 *----------------------------------------------------------------------
 */

MRESULT EXPENTRY
TkOS2ChildProc(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    switch (message) {

	case WM_TRACKFRAME:
#ifdef DEBUG
printf("ChildProc: WM_TRACKFRAME\n");
#endif
            break;
	case WM_CREATE: {
	    CREATESTRUCT *info = (CREATESTRUCT *) PVOIDFROMMP(param2);
	    Tcl_HashEntry *hPtr;
	    int new;
#ifdef DEBUG
printf("Child: WM_CREATE hwnd %x, info %x\n", hwnd, info);
#endif

	    /*
	     * Add the window and handle to the window table.
	     */

	    hPtr = Tcl_CreateHashEntry(&windowTable, (char *)hwnd, &new);
	    if (!new) {
		panic("Duplicate window handle: %p", hwnd);
	    }
	    Tcl_SetHashValue(hPtr, info->pCtlData);

	    /*
	     * Store the pointer to the drawable structure passed into
	     * WinCreateWindow in the user data slot of the window.  Then
	     * set the Z stacking order so the window appears on top.
	     */
	    
	    WinSetWindowULong(hwnd, QWL_USER, (ULONG)info->pCtlData);
	    WinSetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_ZORDER);
	    /*
	    */
	    return 0;
	}

	case WM_DESTROY:
#ifdef DEBUG
printf("Child: WM_DESTROY hwnd %x\n", hwnd);
#endif
            DeleteWindow(hwnd);
	    return 0;
	    
	case WM_ADJUSTWINDOWPOS:
	    return 0;

	case WM_ERASEBACKGROUND:
	    return 0;

	case WM_WINDOWPOSCHANGED: {
#ifdef DEBUG
	    SWP *pos = (SWP *) PVOIDFROMMP(param1);
printf("Child: WM_WINDOWPOSCHANGED, hwnd %x; %d,%d; %dx%d; flags %x\n", hwnd,
pos->x, pos->y, pos->cx, pos->cy, pos->fl);
#endif
	    break;
	}

	case WM_RENDERFMT: {
	    TkOS2Drawable *todPtr;
	    todPtr = (TkOS2Drawable *) WinQueryWindowULong(hwnd, QWL_USER);
#ifdef DEBUG
printf("Child: WM_RENDERFMT hwnd %x, tod %x\n", hwnd, todPtr);
#endif
	    TkOS2ClipboardRender(TkOS2GetWinPtr(todPtr),
	                         (ULONG)(SHORT1FROMMP(param1)));
	    return 0;
	}

	case WM_SHOW:
#ifdef DEBUG
printf("Child: WM_SHOW\n");
#endif
            break;

	case WM_BUTTON1DOWN:
#ifdef DEBUG
printf("Child: WM_BUTTON1DOWN\n");
#endif
	case WM_BUTTON2DOWN:
	case WM_BUTTON3DOWN:
	case WM_BUTTON1UP:
#ifdef DEBUG
printf("Child: WM_BUTTON1UP\n");
#endif
	case WM_BUTTON2UP:
	case WM_BUTTON3UP:
	    TranslateEvent(hwnd, message, param1, param2);
	/*
	 * Pass on BUTTON stuff to default procedure, eg. for having the
	 * focus set to the frame window for us.
	 */
	    break;

	/*
	 * For double-clicks, generate a ButtonPress and ButtonRelease.
	 * Pass on BUTTON stuff to default procedure, eg. for having the
	 * focus set to the frame window for us.
	 */
	case WM_BUTTON1DBLCLK:
	    TranslateEvent(hwnd, WM_BUTTON1DOWN, param1, param2);
	    TranslateEvent(hwnd, WM_BUTTON1UP, param1, param2);
	    break;
	case WM_BUTTON2DBLCLK:
	    TranslateEvent(hwnd, WM_BUTTON2DOWN, param1, param2);
	    TranslateEvent(hwnd, WM_BUTTON2UP, param1, param2);
	    break;
	case WM_BUTTON3DBLCLK:
	    TranslateEvent(hwnd, WM_BUTTON3DOWN, param1, param2);
	    TranslateEvent(hwnd, WM_BUTTON3UP, param1, param2);
	    break;


	case WM_CLOSE:
	    /*
	     * The frame sends the WM_CLOSE to the client (child) if it exists,
	     * so we have to handle it here.
	     */
#ifdef DEBUG
printf("Child: WM_CLOSE\n");
#endif
	case WM_PAINT:
#ifdef DEBUG
printf("Child: WM_PAINT\n");
#endif
	case WM_DESTROYCLIPBOARD:
	case WM_MOUSEMOVE:
	case WM_CHAR:
	/* NOT IN PM, IN WM_CHAR
	case WM_SYSCHAR:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_KEYDOWN:
	case WM_KEYUP:
	*/
	/* One Message
	case WM_SETFOCUS:
	case WM_KILLFOCUS:
	*/
	case WM_SETFOCUS:
	    TranslateEvent(hwnd, message, param1, param2);
	    /* Do not pass on to PM */
	    return 0;
    }

    return WinDefWindowProc(hwnd, message, param1, param2);
}

/*
 *----------------------------------------------------------------------
 *
 * TranslateEvent --
 *
 *	This function is called by the window procedures to handle
 *	the translation from OS/2 PM events to Tk events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues a new Tk event.
 *
 *----------------------------------------------------------------------
 */

static void
TranslateEvent(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    TkWindow *winPtr;
    XEvent event;
    TkOS2Drawable *todPtr;
    HWND hwndTop;

    /*
     * Retrieve the window information, and reset the hwnd pointer in
     * case the original window was a toplevel decorative frame.
     */

    todPtr = (TkOS2Drawable *) WinQueryWindowULong(hwnd, QWL_USER);
    if (todPtr == NULL) {
	return;
    }
    winPtr = TkOS2GetWinPtr(todPtr);

    /*
     * TranslateEvent may get called even after Tk has deleted the window.
     * So we must check for a dead window before proceeding.
     */

    if (winPtr == NULL || winPtr->window == None) {
	return;
    }

    hwndTop = hwnd;
    hwnd = TkOS2GetHWND(winPtr->window);

    event.xany.serial = winPtr->display->request++;
    event.xany.send_event = False;
#ifdef DEBUG
printf("    display %x\n", winPtr->display);
#endif
    event.xany.display = winPtr->display;
    event.xany.window = (Window) winPtr->window;

    switch (message) {
	case WM_PAINT: {
	    HPS hps;
	    RECTL rectl, rectAll;

	    event.type = Expose;
	    hps= WinBeginPaint(hwnd, NULLHANDLE, &rectl);
#ifdef DEBUG
if (hps==NULLHANDLE) {
printf("WinBeginPaint hwnd %x ERROR %x\n", hwnd, WinGetLastError(hab));
} else {
printf("WinBeginPaint hwnd %x is %x\n", hwnd, hps);
}
#endif

	    WinEndPaint(hps);
#ifdef DEBUG
printf("TranslateEvent WM_PAINT hwnd %x, xL=%d, xR=%d, yT=%d, yB=%d\n",
hwnd, rectl.xLeft, rectl.xRight, rectl.yTop, rectl.yBottom);
#endif

	    event.xexpose.x = rectl.xLeft;
	    /*
	     * PM coordinates reversed, we need the distance from the top of
	     * the window to yTop (yAll - yTop)
            if (!WinQueryWindowRect(hwnd, &rectAll)) {
	     */
                /* WinQueryWindowRect failed */
/*
                return;
            }
	    event.xexpose.y = rectAll.yTop - rectl.yTop;
*/
	    event.xexpose.y = TkOS2WindowHeight(todPtr) - rectl.yTop;
	    event.xexpose.width = rectl.xRight - rectl.xLeft;
	    event.xexpose.height = rectl.yTop - rectl.yBottom;
#ifdef DEBUG
printf("       event: x=%d, y=%d, w=%d, h=%d\n",
event.xexpose.x, event.xexpose.y, event.xexpose.width, event.xexpose.height);
#endif
/*
*/
	    event.xexpose.count = 0;
	    break;
	}

	case WM_CLOSE:
#ifdef DEBUG
 printf("TranslateEvent WM_CLOSE hwnd %x\n", hwnd);
#endif
	    event.type = ClientMessage;
	    event.xclient.message_type =
		Tk_InternAtom((Tk_Window) winPtr, "WM_PROTOCOLS");
	    event.xclient.format = 32;
	    event.xclient.data.l[0] =
		Tk_InternAtom((Tk_Window) winPtr, "WM_DELETE_WINDOW");
	    break;

	case WM_SETFOCUS:
/*
#ifdef DEBUG
printf("TranslateEvent WM_SETFOCUS hwnd %x\n", hwnd);
#endif
*/
	    if ( (LOUSHORT(param2)) == TRUE ) {
	    	event.type = FocusIn;
	    } else {
	    	event.type = FocusOut;
	    }
	    event.xfocus.mode = NotifyNormal;
	    event.xfocus.detail = NotifyAncestor;
	    break;
	    
	case WM_DESTROYCLIPBOARD:
/*
#ifdef DEBUG
printf("TranslateEvent WM_DESTROYCLIPBOARD hwnd %x\n", hwnd);
#endif
*/
	    event.type = SelectionClear;
	    event.xselectionclear.selection =
		Tk_InternAtom((Tk_Window)winPtr, "CLIPBOARD");
	    event.xselectionclear.time = WinGetCurrentTime(hab);
	    break;
	    
	case WM_BUTTON1DOWN:
	case WM_BUTTON2DOWN:
	case WM_BUTTON3DOWN:
	case WM_BUTTON1UP:
	case WM_BUTTON2UP:
	case WM_BUTTON3UP:
#ifdef DEBUG
printf("TranslateEvent WM_BUTTON* %x hwnd %x (1D %x, 1U %x)\n", message, hwnd,
WM_BUTTON1DOWN, WM_BUTTON1UP);
#endif
	case WM_MOUSEMOVE:
	case WM_CHAR:
	/* NOT IN PM, IN WM_CHAR
	case WM_SYSCHAR:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_KEYDOWN:
	case WM_KEYUP:
	*/
			{
	    unsigned int state = TkOS2GetModifierState(message,
		    param1, param2);
	    Time time = WinGetCurrentTime(hab);
	    POINTL clientPoint;
	    POINTL rootPoint;
	    BOOL rc;
USHORT vkeycode= SHORT2FROMMP(param2); /* function key */
#ifdef DEBUG
printf("TranslateEvent vkeycode %x, state %x\n", vkeycode, state);
#endif

	    /*
	     * Compute the screen and window coordinates of the event.
	     */
	    
	    rc = WinQueryMsgPos(hab, &rootPoint);
	    clientPoint.x = rootPoint.x;
	    clientPoint.y = rootPoint.y;
	    rc= WinMapWindowPoints (HWND_DESKTOP, hwnd, &clientPoint, 1);

	    /*
	     * Set up the common event fields.
	     */

	    event.xbutton.root = RootWindow(winPtr->display,
		    winPtr->screenNum);
	    event.xbutton.subwindow = None;
	    event.xbutton.x = clientPoint.x;
	    /* PM coordinates reversed */
	    event.xbutton.y = TkOS2WindowHeight((TkOS2Drawable *)todPtr)
	                      - clientPoint.y;
	    event.xbutton.x_root = rootPoint.x;
	    event.xbutton.y_root = yScreen - rootPoint.y;
	    event.xbutton.state = state;
	    event.xbutton.time = time;
	    event.xbutton.same_screen = True;

	    /*
	     * Now set up event specific fields.
	     */

	    switch (message) {
		case WM_BUTTON1DOWN:
		    event.type = ButtonPress;
		    event.xbutton.button = Button1;
		    break;

		case WM_BUTTON2DOWN:
		    event.type = ButtonPress;
		    event.xbutton.button = Button2;
		    break;

		case WM_BUTTON3DOWN:
		    event.type = ButtonPress;
		    event.xbutton.button = Button3;
		    break;
	
		case WM_BUTTON1UP:
		    event.type = ButtonRelease;
		    event.xbutton.button = Button1;
		    break;
	
		case WM_BUTTON2UP:
		    event.type = ButtonRelease;
		    event.xbutton.button = Button2;
		    break;

		case WM_BUTTON3UP:
		    event.type = ButtonRelease;
		    event.xbutton.button = Button3;
		    break;
	
		case WM_MOUSEMOVE:
		    event.type = MotionNotify;
		    event.xmotion.is_hint = NotifyNormal;
		    break;

		/* NOT IN PM, IN WM_CHAR
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
		*/
		    /*
		     * Check for translated characters in the event queue.
		     */

		/*
		    event.type = KeyPress;
		    event.xkey.keycode = param1;
		    GetTranslatedKey(&e.window.event.xkey);
		    break;
		*/

		/* NOT IN PM, IN WM_CHAR
		case WM_SYSKEYUP:
		case WM_KEYUP:
		*/
		    /*
		     * We don't check for translated characters on keyup
		     * because Tk won't know what to do with them.  Instead, we
		     * wait for the WM_CHAR messages which will follow.
		     */
		/*
		    event.type = KeyRelease;
		    event.xkey.keycode = param1;
		    event.xkey.nchars = 0;
		    break;
		*/

		/* NOT IN PM, IN WM_CHAR
                case WM_SYSCHAR:
		*/
		case WM_CHAR: {
		    /*
		     * Careful: for each keypress, two of these messages are
		     * generated, one when pressed and one when released.
		     * When the keyboard-repeat is triggered, multiple key-
		     * down messages can be generated. If this goes faster
		     * than they are retrieved from the queue, they can be
		     * combined in one message.
		     */
		    USHORT flags= SHORT1FROMMP(param1);
		    UCHAR krepeat= CHAR3FROMMP(param1);
		    UCHAR scancode= CHAR4FROMMP(param1);
		    USHORT charcode= SHORT1FROMMP(param2);
		    USHORT vkeycode= SHORT2FROMMP(param2); /* function key */
		    int loop;
		    /*
		     * Check for translated characters in the event queue.
		     * and more than 1 char in the message
		     */
		    if ( (flags & KC_ALT) && !(flags & KC_CTRL) &&
		         (vkeycode != VK_CTRL) && (vkeycode != VK_F10) ) {
		        /* Equivalent to Windows' WM_SYSKEY */
#ifdef DEBUG
printf("Equivalent of WM_SYSKEY...\n");
#endif
		    }
		    if ( flags & KC_KEYUP ) {
		    	/* Key Up */
#ifdef DEBUG
printf("KeyUp\n");
#endif
		    	event.type = KeyRelease;
		    } else {
		    	/* Key Down */
#ifdef DEBUG
printf("KeyDown\n");
#endif
		    	event.type = KeyPress;
		    }
		    if ( flags & KC_VIRTUALKEY ) {
		    	/* vkeycode is valid, should be given precedence */
		    	event.xkey.keycode = vkeycode;
#ifdef DEBUG
printf("virtual keycode %x\n", vkeycode);
#endif
		    } else {
		    	event.xkey.keycode = 0;
		    }
		    if ( flags & KC_CHAR ) {
		    	/* charcode is valid */
		    	event.xkey.nchars = krepeat;
		    	for ( loop=0; loop < krepeat; loop++ ) {
		    		event.xkey.trans_chars[loop] = charcode;
#ifdef DEBUG
printf("charcode %x\n", charcode);
#endif
		    	}
		    } else {
		    	event.xkey.nchars = 0;
		   	event.xkey.trans_chars[0] = 0;
		    }
                    /*
                     * Synthesize both a KeyPress and a KeyRelease.
                    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
                    event.type = KeyRelease;
                     */

		    break;
	    	    }
	    }

	    if ((event.type == MotionNotify)
		    || (event.type == ButtonPress)
		    || (event.type == ButtonRelease)) {
		TkOS2PointerEvent(&event, winPtr);
		return;
	    }
	    break;
	}

	default:
	    return;
    }
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2GetModifierState --
 *
 *	This function constructs a state mask for the mouse buttons 
 *	and modifier keys.
 *
 * Results:
 *	Returns a composite value of all the modifier and button state
 *	flags that were set at the time the event occurred.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned int
TkOS2GetModifierState(message, param1, param2)
    ULONG message;		/* OS/2 PM message type */
    MPARAM param1;		/* param1 of message, used if key message */
    MPARAM param2;		/* param2 of message, used if key message */
{
    unsigned int state = 0;	/* accumulated state flags */
    int isKeyEvent = 0;		/* 1 if message is a key press or release */
    int prevState = 0;		/* 1 if key was previously down */
    USHORT flags= SHORT1FROMMP(param1);
    UCHAR krepeat= CHAR3FROMMP(param1);
    UCHAR scancode= CHAR4FROMMP(param1);
    USHORT charcode= SHORT1FROMMP(param2);
    USHORT vkeycode= SHORT2FROMMP(param2); /* function key */

#ifdef DEBUG
printf("TkOS2GetModifierState fl %x, krepeat %d, scan %x, char %d, VK %x\n",
flags, krepeat, scancode, charcode, vkeycode);
#endif
    /*
     * If the event is a key press or release, we check for autorepeat.
     */
    if ( (flags & KC_CHAR) || (flags & KC_VIRTUALKEY) ) {
	isKeyEvent = TRUE;
	prevState = flags & KC_PREVDOWN;
#ifdef DEBUG
printf("    isKeyEvent\n");
#endif
    }

    /*
     * If the key being pressed or released is a modifier key, then
     * we use its previous state, otherwise we look at the current state.
     */

    if (isKeyEvent && (vkeycode == VK_SHIFT)) {
	state |= prevState ? ShiftMask : 0;
    } else {
	state |= (WinGetKeyState(HWND_DESKTOP, VK_SHIFT) & 0x8000)
	         ? ShiftMask : 0;
    }
    if (isKeyEvent && (vkeycode == VK_CTRL)) {
	state |= prevState ? ControlMask : 0;
    } else {
	state |= (WinGetKeyState(HWND_DESKTOP, VK_CTRL) & 0x8000)
	         ? ControlMask : 0;
    }
    if (isKeyEvent && (vkeycode == VK_ALT)) {
	state |= prevState ? Mod1Mask : 0;
    } else {
	state |= (WinGetKeyState(HWND_DESKTOP, VK_ALT) & 0x8000)
	         ? Mod1Mask : 0;
    }
    if (isKeyEvent && (vkeycode == VK_MENU)) {
	state |= prevState ? Mod2Mask : 0;
    } else {
	state |= (WinGetKeyState(HWND_DESKTOP, VK_MENU) & 0x8000)
	         ? Mod2Mask : 0;
    }

    /*
     * For toggle keys, we have to check both the previous key state
     * and the current toggle state.  The result is the state of the
     * toggle before the event.
     */

    if ((vkeycode == VK_CAPSLOCK) && !( flags & KC_KEYUP)) {
	state = (prevState ^ (WinGetKeyState(HWND_DESKTOP, VK_CAPSLOCK) & 0x0001))
	        ? 0 : LockMask;
    } else {
	state |= (WinGetKeyState(HWND_DESKTOP, VK_CAPSLOCK) & 0x0001)
	         ? LockMask : 0;
    }
    if ((vkeycode == VK_NUMLOCK) && !( flags & KC_KEYUP)) {
	state = (prevState ^ (WinGetKeyState(HWND_DESKTOP, VK_NUMLOCK) & 0x0001))
	        ? 0 : Mod1Mask;
    } else {
	state |= (WinGetKeyState(HWND_DESKTOP, VK_NUMLOCK) & 0x0001)
	         ? Mod1Mask : 0;
    }
    if ((vkeycode == VK_SCRLLOCK) && !( flags & KC_KEYUP)) {
	state = (prevState ^ (WinGetKeyState(HWND_DESKTOP, VK_SCRLLOCK) & 0x0001))
	        ? 0 : Mod3Mask;
    } else {
	state |= (WinGetKeyState(HWND_DESKTOP, VK_SCRLLOCK) & 0x0001)
	         ? Mod3Mask : 0;
    }

    /*
     * If a mouse button is being pressed or released, we use the previous
     * state of the button.
     */

    if (message == WM_BUTTON1UP || (message != WM_BUTTON1DOWN
	    && WinGetKeyState(HWND_DESKTOP, VK_BUTTON1) & 0x8000)) {
	state |= Button1Mask;
    }
    if (message == WM_BUTTON2UP || (message != WM_BUTTON2DOWN
	    && WinGetKeyState(HWND_DESKTOP, VK_BUTTON2) & 0x8000)) {
	state |= Button2Mask;
    }
    if (message == WM_BUTTON3UP || (message != WM_BUTTON3DOWN
	    && WinGetKeyState(HWND_DESKTOP, VK_BUTTON3) & 0x8000)) {
	state |= Button3Mask;
    }
#ifdef DEBUG
printf("    returning state %x\n", state);
#endif
    return state;
}

/*
 *----------------------------------------------------------------------
 *
 * GetTranslatedKey --		NOT USED
 *
 *	Retrieves WM_CHAR messages that are placed on the system queue
 *	by the TranslateMessage system call and places them in the
 *	given KeyPress event.
 *
 * Results:
 *	Sets the trans_chars and nchars member of the key event.
 *
 * Side effects:
 *	Removes any WM_CHAR messages waiting on the top of the system
 *	event queue.
 *
 *----------------------------------------------------------------------
 */

static void
GetTranslatedKey(xkey)
    XKeyEvent *xkey;
{
    QMSG msg;
    
    xkey->nchars = 0;

    while (xkey->nchars < XMaxTransChars
	    && WinPeekMsg(hab, &msg, NULLHANDLE, 0, 0, PM_NOREMOVE)) {
/* WM_SYSCHAR not in PM
	if ((msg.msg == WM_CHAR) || msg.msg == WM_SYSCHAR) {
*/
	if ((msg.msg == WM_CHAR)) {
	    xkey->trans_chars[xkey->nchars] = SHORT1FROMMP(msg.mp2);
	    xkey->nchars++;
	    WinGetMsg(hab, &msg, NULLHANDLE, 0, 0);
            if ((msg.msg == WM_CHAR) && ((LONG)msg.mp2 & 0x20000000)) {
                xkey->state = 0;
            }
	} else {
	    break;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2GetDrawableFromHandle --
 *
 *	Find the drawable associated with the given window handle.
 *
 * Results:
 *	Returns a drawable pointer if the window is managed by Tk.
 *	Otherwise it returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkOS2Drawable *
TkOS2GetDrawableFromHandle(hwnd)
    HWND hwnd;			/* OS/2 PM window handle */
{
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&windowTable, (char *)hwnd);
    if (hPtr) {
	return (TkOS2Drawable *)Tcl_GetHashValue(hPtr);
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * DeleteWindow --
 *
 *      Remove a window from the window table, and free the resources
 *      associated with the drawable.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Frees the resources associated with a window handle.
 *
 *----------------------------------------------------------------------
 */

static void
DeleteWindow(hwnd)
    HWND hwnd;
{
    TkOS2Drawable *todPtr;
    Tcl_HashEntry *hPtr;

    /*
     * Remove the window from the window table.
     */

    hPtr = Tcl_FindHashEntry(&windowTable, (char *)hwnd);
    if (hPtr) {
        Tcl_DeleteHashEntry(hPtr);
    }

    /*
     * Free the drawable associated with this window, unless the drawable
     * is still in use by a TkWindow.  This only happens in the case of
     * a top level window, since the window gets destroyed when the
     * decorative frame is destroyed.
     * NOTE: at least with EMX, MT-library, a freed TkWindow gets a value of
     * 0x61616161 in flags. To not get a panic in such a case, we look for a
     * value bigger than the union of all possible flags.
     * Ditto for winPtr->display.
     */

    todPtr = (TkOS2Drawable *) WinQueryWindowULong(hwnd, QWL_USER);
#ifdef DEBUG
printf("DeleteWindow: hwnd %x, todPtr %x\n", hwnd, todPtr);
#endif
    if (todPtr) {
#ifdef DEBUG
printf("              todPtr->window.winPtr %x\n", todPtr->window.winPtr);
#endif
        if (todPtr->window.winPtr == NULL) {
            ckfree((char *) todPtr);
            todPtr= NULL;
        } else if (!(todPtr->window.winPtr->flags & TK_TOP_LEVEL) &&
                   (todPtr->window.winPtr->flags > (TK_MAPPED | TK_TOP_LEVEL
                     | TK_ALREADY_DEAD | TK_NEED_CONFIG_NOTIFY | TK_GRAB_FLAG
                     | TK_CHECKED_IC | TK_PARENT_DESTROYED
                     | TK_WM_COLORMAP_WINDOW ) )
                  ) {
#ifdef DEBUG
printf("     PANIC    flags %x, todPtr->window.handle %x\n",
todPtr->window.winPtr->flags, todPtr->window.handle);
#endif
            panic("Non-toplevel window destroyed before its drawable");
        } else {
#ifdef DEBUG
printf(" EMX buggaroo: display %x\n", todPtr->window.winPtr->display);
#endif
            if (todPtr->window.winPtr->display == (Display *)0x61616161) {
                todPtr->window.handle = NULLHANDLE;
                todPtr->window.winPtr = NULL;
            }
        }
    }
}
