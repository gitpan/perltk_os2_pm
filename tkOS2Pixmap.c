/* 
 * tkOS2Pixmap.c --
 *
 *	This file contains the Xlib emulation functions pertaining to
 *	creating and destroying pixmaps.
 *
 * Copyright (c) 1996-1997 Illya Vaes
 * Copyright (c) 1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkOS2Int.h"


/*
 *----------------------------------------------------------------------
 *
 * Tk_GetPixmap --
 *
 *	Creates an in memory drawing surface.
 *
 * Results:
 *	Returns a handle to a new pixmap.
 *
 * Side effects:
 *	Allocates a new OS/2 bitmap, hps, DC.
 *
 *----------------------------------------------------------------------
 */

Pixmap
Tk_GetPixmap(display, d, width, height, depth)
    Display* display;
    Drawable d;
    int width;
    int height;
    int depth;
{
    TkOS2Drawable *newTodPtr, *todPtr;
    BITMAPINFOHEADER2 bmpInfo;
    LONG rc;
    DEVOPENSTRUC dop = {0L, (PSZ)"DISPLAY", NULL, 0L, 0L, 0L, 0L, 0L, 0L};
    SIZEL sizl = {0,0}; /* use same page size as device */
/*
SIZEL sizl;
*/
sizl.cx = width; sizl.cy = height;
    
    display->request++;

    newTodPtr = (TkOS2Drawable*) ckalloc(sizeof(TkOS2Drawable));
    if (newTodPtr == NULL) {
	return (Pixmap)None;
    }

    newTodPtr->type = TOD_BITMAP;
    newTodPtr->bitmap.depth = depth;
    todPtr = (TkOS2Drawable *)d;
    if (todPtr->type != TOD_BITMAP) {
#ifdef DEBUG
printf("Tk_GetPixmap %x, depth %d, parent %x, %dx%d\n", newTodPtr, depth,
todPtr->window.handle, sizl.cx, sizl.cy);
#endif
        newTodPtr->bitmap.parent = todPtr->window.handle;
        if (todPtr->window.winPtr == NULL) {
            newTodPtr->bitmap.colormap = DefaultColormap(display,
                    DefaultScreen(display));
        } else {
            newTodPtr->bitmap.colormap = todPtr->window.winPtr->atts.colormap;
        }
    } else {
#ifdef DEBUG
printf("Tk_GetPixmap %x, depth %d, parent (bitmap) %x, %dx%d\n", newTodPtr,
depth, todPtr->bitmap.parent, sizl.cx, sizl.cy);
#endif
        newTodPtr->bitmap.colormap = todPtr->bitmap.colormap;
        newTodPtr->bitmap.parent = todPtr->bitmap.parent;
    }
    /*
    bmpInfo.cbFix = sizeof(BITMAPINFOHEADER2);
    */
    bmpInfo.cbFix = 16L;
    bmpInfo.cx = width;
    bmpInfo.cy = height;
    bmpInfo.cPlanes = 1;
    bmpInfo.cBitCount = depth;
    newTodPtr->bitmap.dc = DevOpenDC(hab, OD_MEMORY, (PSZ)"*", 5L,
                                     (PDEVOPENDATA)&dop, NULLHANDLE);
    if (newTodPtr->bitmap.dc == DEV_ERROR) {
#ifdef DEBUG
printf("DevOpenDC failed in Tk_GetPixmap\n");
#endif
        ckfree((char *) newTodPtr);
        return (Pixmap)None;
    }
#ifdef DEBUG
printf("DevOpenDC in Tk_GetPixmap returns %x\n", newTodPtr->bitmap.dc);
#endif
    newTodPtr->bitmap.hps = GpiCreatePS(hab, newTodPtr->bitmap.dc, &sizl,
                                        PU_PELS | GPIT_NORMAL | GPIA_ASSOC);
    if (newTodPtr->bitmap.hps == GPI_ERROR) {
        DevCloseDC(newTodPtr->bitmap.dc);
#ifdef DEBUG
printf("GpiCreatePS failed in Tk_GetPixmap\n");
#endif
        ckfree((char *) newTodPtr);
        return (Pixmap)None;
    }
#ifdef DEBUG
printf("GpiCreatePS in Tk_GetPixmap returns %x\n", newTodPtr->bitmap.hps);
#endif
    newTodPtr->bitmap.handle = GpiCreateBitmap(newTodPtr->bitmap.hps,
                                               &bmpInfo, 0L, NULL, NULL);

    if (newTodPtr->bitmap.handle == NULLHANDLE) {
#ifdef DEBUG
printf("GpiCreateBitmap ERROR %x in Tk_GetPixmap\n", WinGetLastError(hab));
#endif
	ckfree((char *) newTodPtr);
	return (Pixmap)None;
    }
#ifdef DEBUG
printf("GpiCreateBitmap in Tk_GetPixmap returns %x\n", newTodPtr->bitmap.handle);
#endif
    sizl.cx = width;
    sizl.cy = height;
    rc = GpiSetBitmapDimension(newTodPtr->bitmap.handle, &sizl);
#ifdef DEBUG
if (rc == FALSE) {
printf("    GpiSetBitmapDimension ERROR, lastError %x\n", WinGetLastError(hab));
} else {
printf("    GpiSetBitmapDimension: %dx%d\n", sizl.cx, sizl.cy);
}
rc = GpiQueryBitmapDimension(newTodPtr->bitmap.handle, &sizl);
if (rc == FALSE) {
printf("    GpiQueryBitmapDimension ERROR, lastError %x\n", WinGetLastError(hab));
} else {
printf("    GpiQueryBitmapDimension: %dx%d\n", sizl.cx, sizl.cy);
}
#endif
    rc = GpiSetBitmap(newTodPtr->bitmap.hps, newTodPtr->bitmap.handle);
    if (rc == HBM_ERROR) {
#ifdef DEBUG
printf("GpiSetBitmap returned HBM_ERROR, lastError %x\n", WinGetLastError(hab));
#endif
        GpiDestroyPS(newTodPtr->bitmap.hps);
        DevCloseDC(newTodPtr->bitmap.dc);
        ckfree((char *) newTodPtr);
        return (Pixmap)None;
    }
#ifdef DEBUG
else printf("GpiSetBitmap %x into hps %x returns %x\n",
newTodPtr->bitmap.handle, newTodPtr->bitmap.hps, rc);
#endif

    return (Pixmap)newTodPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_FreePixmap --
 *
 *	Release the resources associated with a pixmap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deletes the bitmap created by Tk_GetPixmap.
 *
 *----------------------------------------------------------------------
 */

void
Tk_FreePixmap(display, pixmap)
    Display* display;
    Pixmap pixmap;
{
    TkOS2Drawable *todPtr = (TkOS2Drawable *) pixmap;
    HBITMAP hbm;

#ifdef DEBUG
printf("Tk_FreePixmap %x\n", todPtr);
#endif

    display->request++;
    if (todPtr != NULL) {
        hbm = GpiSetBitmap(todPtr->bitmap.hps, NULLHANDLE);
#ifdef DEBUG
printf("    GpiSetBitmap hps %x returned %x\n", todPtr->bitmap.hps, hbm);
#endif
	GpiDeleteBitmap(todPtr->bitmap.handle);
        GpiDestroyPS(todPtr->bitmap.hps);
        DevCloseDC(todPtr->bitmap.dc);
	ckfree((char *)todPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkSetPixmapColormap --
 *
 *      The following function is a hack used by the photo widget to
 *      explicitly set the colormap slot of a Pixmap.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
TkSetPixmapColormap(pixmap, colormap)
    Pixmap pixmap;
    Colormap colormap;
{
    TkOS2Drawable *todPtr = (TkOS2Drawable *)pixmap;

#ifdef DEBUG
printf("TkSetPixmapColormap, bitmap %x, colormap %x\n", TkOS2GetHBITMAP(todPtr),
        colormap);
#endif
    todPtr->bitmap.colormap = colormap;
    /*
    rc = (HPAL) TkOS2SelectPalette(todPtr->bitmap.hps, todPtr->bitmap.parent,
                                   colormap);
    */
}
