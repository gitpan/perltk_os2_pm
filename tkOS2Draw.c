/* 
 * tkOS2Draw.c --
 *
 *	This file contains the Xlib emulation functions pertaining to
 *	actually drawing objects on a window.
 *
 * Copyright (c) 1996-1997 Illya Vaes
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1994 Software Research Associates, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkOS2Int.h"

#define PI 3.14159265358979
#define XAngleToRadians(a) ((double)(a) / 64 * PI / 180)

/*
 * Translation table between X gc functions and OS/2 GPI mix attributes.
 */

static int mixModes[] = {
    FM_ZERO,			/* GXclear */
    FM_AND,			/* GXand */
    FM_MASKSRCNOT,		/* GXandReverse */
    FM_OVERPAINT,		/* GXcopy */
    FM_SUBTRACT,		/* GXandInverted */
    FM_LEAVEALONE,		/* GXnoop */
    FM_XOR,			/* GXxor */
    FM_OR,			/* GXor */
    FM_NOTMERGESRC,		/* GXnor */
    FM_NOTXORSRC,		/* GXequiv */
    FM_INVERT,			/* GXinvert */
    FM_MERGESRCNOT,		/* GXorReverse */
    FM_NOTCOPYSRC,		/* GXcopyInverted */
    FM_MERGENOTSRC,		/* GXorInverted */
    FM_NOTMASKSRC,		/* GXnand */
    FM_ONE			/* GXset */
};

static int backMixModes[] = {
    BM_LEAVEALONE,		/* GXclear */
    BM_LEAVEALONE,		/* GXand */
    BM_LEAVEALONE,		/* GXandReverse */
    BM_OVERPAINT,		/* GXcopy */
    BM_SUBTRACT,		/* GXandInverted */
    BM_LEAVEALONE,		/* GXnoop */
    BM_XOR,			/* GXxor */
    BM_OR,			/* GXor */
    BM_LEAVEALONE,		/* GXnor */
    BM_LEAVEALONE,		/* GXequiv */
    BM_LEAVEALONE,		/* GXinvert */
    BM_LEAVEALONE,		/* GXorReverse */
    BM_LEAVEALONE,		/* GXcopyInverted */
    BM_LEAVEALONE,		/* GXorInverted */
    BM_LEAVEALONE,		/* GXnand */
    BM_LEAVEALONE		/* GXset */
};


/*
 * Translation table between X gc functions and OS/2 GPI BitBlt raster op modes.
 * Some of the operations defined in X don't have names, so we have to construct
 * new opcodes for those functions.  This is arcane and probably not all that
 * useful, but at least it's accurate.
 */

#define NOTSRCAND	(LONG)0x0022 /* dest = (NOT source) AND dest */
#define NOTSRCINVERT	(LONG)0x0099 /* dest = (NOT source) XOR dest */
#define SRCORREVERSE	(LONG)0x00dd /* dest = source OR (NOT dest) */
#define SRCNAND		(LONG)0x0077 /* dest = (NOT source) OR (NOT dest) */

static int bltModes[] = {
    ROP_ZERO,			/* GXclear */
    ROP_SRCAND,			/* GXand */
    ROP_SRCERASE,		/* GXandReverse */
    ROP_SRCCOPY,		/* GXcopy */
    NOTSRCAND,			/* GXandInverted */
    ROP_PATCOPY,		/* GXnoop */
    ROP_SRCINVERT,		/* GXxor */
    ROP_SRCPAINT,		/* GXor */
    ROP_NOTSRCERASE,		/* GXnor */
    NOTSRCINVERT,		/* GXequiv */
    ROP_DSTINVERT,		/* GXinvert */
    SRCORREVERSE,		/* GXorReverse */
    ROP_NOTSRCCOPY,		/* GXcopyInverted */
    ROP_MERGEPAINT,		/* GXorInverted */
    SRCNAND,			/* GXnand */
    ROP_ONE			/* GXset */
};

/*
 * The following raster op uses the source bitmap as a mask for the
 * pattern.  This is used to draw in a foreground color but leave the
 * background color transparent.
 */

#define MASKPAT		0x00e2 /* dest = (src & pat) | (!src & dst) */

/*
 * The following two raster ops are used to copy the foreground and background
 * bits of a source pattern as defined by a stipple used as the pattern.
 */

#define COPYFG		0x00ca /* dest = (pat & src) | (!pat & dst) */
#define COPYBG		0x00ac /* dest = (!pat & src) | (pat & dst) */

/*
 * Macros used later in the file.
 */

#ifndef MIN
#define MIN(a,b)	((a>b) ? b : a)
#endif
#ifndef MAX
#define MAX(a,b)	((a<b) ? b : a)
#endif

/*
 * Forward declarations for procedures defined in this file:
 */

static POINTL *		ConvertPoints (Drawable d, XPoint *points, int npoints,
			    int mode, RECTL *bbox);
static void		DrawOrFillArc (Display *display,
			    Drawable d, GC gc, int x, int y,
			    unsigned int width, unsigned int height,
			    int angle1, int angle2, int fill);
static void		RenderObject (HPS hps, GC gc, Drawable d,
                            XPoint* points, int npoints, int mode,
                            PLINEBUNDLE lineBundle, int func);
static void		TkOS2SetStipple(HPS destPS, HPS bmpPS, HBITMAP stipple,
			    LONG x, LONG y, LONG *oldPatternSet,
			    PPOINTL oldRefPoint);
static void		TkOS2UnsetStipple(HPS destPS, HPS bmpPS, HBITMAP stipple,
			    LONG oldPatternSet, PPOINTL oldRefPoint);

/*
 *----------------------------------------------------------------------
 *
 * TkOS2GetDrawablePS --
 *
 *	Retrieve the Presentation Space from a drawable.
 *
 * Results:
 *	Returns the window PS for windows.  Returns a new memory PS
 *	for pixmaps.
 *
 * Side effects:
 *	Sets up the palette for the presentation space, and saves the old
 *	presentation space state in the passed in TkOS2PSState structure.
 *
 *----------------------------------------------------------------------
 */

HPS
TkOS2GetDrawablePS(display, d, state)
    Display *display;
    Drawable d;
    TkOS2PSState* state;
{
    HPS hps;
    TkOS2Drawable *todPtr = (TkOS2Drawable *)d;
    Colormap cmap;

    if (todPtr->type != TOD_BITMAP) {
        TkWindow *winPtr = todPtr->window.winPtr;

	hps = WinGetPS(todPtr->window.handle);
/*
#ifdef DEBUG
printf("TkOS2GetDrawablePS window %x (handle %x, hps %x)\n", d,
todPtr->window.handle, hps);
#endif
*/
        if (winPtr == NULL) {
	    cmap = DefaultColormap(display, DefaultScreen(display));
        } else {
	    cmap = winPtr->atts.colormap;
        }
        state->palette = TkOS2SelectPalette(hps, todPtr->window.handle, cmap);
    } else {

        hps = todPtr->bitmap.hps;
/*
#ifdef DEBUG
printf("TkOS2GetDrawablePS bitmap %x (handle %x, hps %x)\n", d,
todPtr->bitmap.handle, hps);
#endif
*/
        cmap = todPtr->bitmap.colormap;
        state->palette = TkOS2SelectPalette(hps, todPtr->bitmap.parent, cmap);
    }
    return hps;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2ReleaseDrawablePS --
 *
 *	Frees the resources associated with a drawable's DC.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Restores the old bitmap handle to the memory DC for pixmaps.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2ReleaseDrawablePS(d, hps, state)
    Drawable d;
    HPS hps;
    TkOS2PSState *state;
{
    ULONG changed;
    HPAL oldPal;
    TkOS2Drawable *todPtr = (TkOS2Drawable *)d;

    oldPal = GpiSelectPalette(hps, state->palette);
#ifdef DEBUG
if (oldPal == PAL_ERROR) printf("GpiSelectPalette TkOS2ReleaseDrawablePS PAL_ERROR: %x\n",
WinGetLastError(hab));
else printf("GpiSelectPalette TkOS2ReleaseDrawablePS: %x\n", oldPal);
#endif
    if (todPtr->type != TOD_BITMAP) {

/*
#ifdef DEBUG
printf("TkOS2ReleaseDrawablePS window %x\n", d);
#endif
*/
        WinRealizePalette(TkOS2GetHWND(d), hps, &changed);
        WinReleasePS(hps);
    } else {
/*
#ifdef DEBUG
printf("TkOS2ReleaseDrawablePS bitmap %x released %x\n", d, state->bitmap);
#endif
*/
        WinRealizePalette(todPtr->bitmap.parent, hps, &changed);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ConvertPoints --
 *
 *	Convert an array of X points to an array of OS/2 GPI points.
 *
 * Results:
 *	Returns the converted array of POINTLs.
 *
 * Side effects:
 *	Allocates a block of memory that should not be freed.
 *
 *----------------------------------------------------------------------
 */

static POINTL *
ConvertPoints(d, points, npoints, mode, bbox)
    Drawable d;
    XPoint *points;
    int npoints;
    int mode;			/* CoordModeOrigin or CoordModePrevious. */
    RECTL *bbox;			/* Bounding box of points. */
{
    static POINTL *os2Points = NULL; /* Array of points that is reused. */
    static int nOS2Points = -1;	    /* Current size of point array. */
    LONG windowHeight;
    int i;

#ifdef DEBUG
printf("ConvertPoints\n");
#endif
/*
*/

    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)d);

    /*
     * To avoid paying the cost of a malloc on every drawing routine,
     * we reuse the last array if it is large enough.
     */

    if (npoints > nOS2Points) {
	if (os2Points != NULL) {
#ifdef DEBUG
printf("    ckfree os2Points %x\n", os2Points);
#endif
	    ckfree((char *) os2Points);
	}
	os2Points = (POINTL *) ckalloc(sizeof(POINTL) * npoints);
#ifdef DEBUG
printf("    ckalloc os2Points %x\n", os2Points);
#endif
	if (os2Points == NULL) {
	    nOS2Points = -1;
	    return NULL;
	}
	nOS2Points = npoints;
    }

    /* Convert to PM Coordinates */
    bbox->xLeft = bbox->xRight = points[0].x;
    bbox->yTop = bbox->yBottom = windowHeight - points[0].y;
    
    if (mode == CoordModeOrigin) {
	for (i = 0; i < npoints; i++) {
	    os2Points[i].x = points[i].x;
	    /* convert to PM */
	    os2Points[i].y = windowHeight - points[i].y;
	    bbox->xLeft = MIN(bbox->xLeft, os2Points[i].x);
	    bbox->xRight = MAX(bbox->xRight, os2Points[i].x);
	    /* y: min and max switched for PM */
	    bbox->yTop = MAX(bbox->yTop, os2Points[i].y);
	    bbox->yBottom = MIN(bbox->yBottom, os2Points[i].y);
/*
#ifdef DEBUG
printf("       point %d: x %d, y %d; bbox xL %d, xR %d, yT %d, yB %d\n", i,
points[i].x, points[i].y, bbox->xLeft, bbox->xRight, bbox->yTop, bbox->yBottom);
#endif
*/
	}
    } else {
	os2Points[0].x = points[0].x;
	os2Points[0].y = windowHeight - points[0].y;
	for (i = 1; i < npoints; i++) {
	    os2Points[i].x = os2Points[i-1].x + points[i].x;
	    /* convert to PM */
	    os2Points[i].y = os2Points[i-1].y +
	                     (windowHeight - points[i].y);
	    bbox->xLeft = MIN(bbox->xLeft, os2Points[i].x);
	    bbox->xRight = MAX(bbox->xRight, os2Points[i].x);
	    /* y: min and max switched for PM */
	    bbox->yTop = MAX(bbox->yTop, os2Points[i].y);
	    bbox->yBottom = MIN(bbox->yBottom, os2Points[i].y);
/*
#ifdef DEBUG
printf("       point %d: x %d, y %d; bbox xL %d, xR %d, yT %d, yB %d\n",
points[i].x, points[i].y, bbox->xLeft, bbox->xRight, bbox->yTop, bbox->yBottom);
printf("       os2point: x %d, y %d\n", os2Points[i].x, os2Points[i].y);
#endif
*/
	}
    }
    return os2Points;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyArea --
 *
 *	Copies data from one drawable to another using block transfer
 *	routines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Data is moved from a window or bitmap to a second window or
 *	bitmap.
 *
 *----------------------------------------------------------------------
 */

void
XCopyArea(display, src, dest, gc, src_x, src_y, width, height, dest_x, dest_y)
    Display* display;
    Drawable src;
    Drawable dest;
    GC gc;
    int src_x, src_y;
    unsigned int width, height;
    int dest_x, dest_y;
{
    HPS srcPS, destPS;
    TkOS2PSState srcState, destState;
    POINTL aPoints[3]; /* Lower-left, upper-right, lower-left source */
    BOOL rc;
    LONG windowHeight;

#ifdef DEBUG
    printf("XCopyArea (%d,%d) -> (%d,%d), w %d, h %d\n", src_x, src_y,
           dest_x, dest_y, width, height);
#endif
    /* Translate the Y coordinates to PM coordinates */
    /* Determine height of window */
    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)dest);
    aPoints[0].x = dest_x;
    aPoints[0].y = windowHeight - dest_y - height;
    aPoints[1].x = dest_x + width;
    aPoints[1].y = windowHeight - dest_y;
    aPoints[2].x = src_x;
    if (src != dest) {
        windowHeight = TkOS2WindowHeight((TkOS2Drawable *)src);
    }
    aPoints[2].y = windowHeight - src_y - height;
    srcPS = TkOS2GetDrawablePS(display, src, &srcState);
#ifdef DEBUG
    printf("    PM: (%d,%d)-(%d,%d) <- (%d,%d)\n", aPoints[0].x, aPoints[0].y,
           aPoints[1].x, aPoints[1].y, aPoints[2].x, aPoints[2].y);
#endif

    if (src != dest) {
	destPS = TkOS2GetDrawablePS(display, dest, &destState);
    } else {
	destPS = srcPS;
    }
#ifdef DEBUG
rc = GpiRectVisible(destPS, (PRECTL)&aPoints[0]);
if (rc==RVIS_PARTIAL || rc==RVIS_VISIBLE)
printf("GpiRectVisible (%d,%d) (%d,%d) (partially) visible\n",
aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y);
else if (rc==RVIS_INVISIBLE) printf("GpiRectVisible (%d,%d) (%d,%d) invisible\n",
aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y);
else printf("GpiRectVisible (%d,%d) (%d,%d) ERROR, error %x\n",
aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y, WinGetLastError(hab));
#endif
/*
*/
    rc = GpiBitBlt(destPS, srcPS, 3, aPoints, bltModes[gc->function],
                   BBO_IGNORE);
#ifdef DEBUG
printf("    srcPS %x, type %s, destPS %x, type %s\n", srcPS,
((TkOS2Drawable *)src)->type == TOD_BITMAP ? "bitmap" : "window", destPS,
((TkOS2Drawable *)dest)->type == TOD_BITMAP ? "bitmap" : "window");
printf("    GpiBitBlt %x -> %x, 3, (%d,%d) (%d,%d) (%d,%d), %x returns %d\n",
srcPS, destPS, aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y,
aPoints[2].x, aPoints[2].y, bltModes[gc->function], rc);
#endif
/*
*/

    if (src != dest) {
	TkOS2ReleaseDrawablePS(dest, destPS, &destState);
    }
    TkOS2ReleaseDrawablePS(src, srcPS, &srcState);
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyPlane --
 *
 *	Copies a bitmap from a source drawable to a destination
 *	drawable.  The plane argument specifies which bit plane of
 *	the source contains the bitmap.  Note that this implementation
 *	ignores the gc->function.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the destination drawable.
 *
 *----------------------------------------------------------------------
 */

void
XCopyPlane(display, src, dest, gc, src_x, src_y, width, height, dest_x,
	dest_y, plane)
    Display* display;
    Drawable src;
    Drawable dest;
    GC gc;
    int src_x, src_y;
    unsigned int width, height;
    int dest_x, dest_y;
    unsigned long plane;
{
    HPS srcPS, destPS;
    TkOS2PSState srcState, destState;
    LONG bgPattern, fgPattern, oldPattern;
    LONG oldMix, oldBackMix;
    LONG oldColor, oldBackColor;
    LONG srcWindowHeight, destWindowHeight;
    POINTL aPoints[3]; /* Lower-left, upper-right, lower-left source */
    LONG rc;

#ifdef DEBUG
    printf("XCopyPlane (%d,%d) -> (%d,%d), w %d, h %d; fg %d, bg %d\n",
    src_x, src_y, dest_x, dest_y, width, height, gc->foreground, gc->background);
#endif

    /* Translate the Y coordinates to PM coordinates */
    destWindowHeight = TkOS2WindowHeight((TkOS2Drawable *)dest);
    if (src != dest) {
        srcWindowHeight = TkOS2WindowHeight((TkOS2Drawable *)src);
    } else {
        srcWindowHeight = destWindowHeight;
    }
#ifdef DEBUG
printf("srcWindowHeight %d, destWindowHeight %d\n", srcWindowHeight,
destWindowHeight);
#endif
    aPoints[0].x = dest_x;
    aPoints[0].y = destWindowHeight - dest_y - height;
    aPoints[1].x = dest_x + width;
    aPoints[1].y = destWindowHeight - dest_y;
    aPoints[2].x = src_x;
    aPoints[2].y = src_y;
    display->request++;

    if (plane != 1) {
	panic("Unexpected plane specified for XCopyPlane");
    }

    srcPS = TkOS2GetDrawablePS(display, src, &srcState);

    if (src != dest) {
	destPS = TkOS2GetDrawablePS(display, dest, &destState);
    } else {
	destPS = srcPS;
    }
#ifdef DEBUG
    printf("    srcPS %x, type %s, destPS %x, type %s, clip_mask %x\n", srcPS,
           ((TkOS2Drawable *)src)->type == TOD_BITMAP ? "bitmap" : "window",
           destPS,
           ((TkOS2Drawable *)dest)->type == TOD_BITMAP ? "bitmap" : "window",
           gc->clip_mask);
    printf("    (%d,%d) (%d,%d) (%d,%d)\n", aPoints[0].x, aPoints[0].y,
           aPoints[1].x, aPoints[1].y, aPoints[2].x, aPoints[2].y);
#endif

    if (gc->clip_mask == src) {
#ifdef DEBUG
        printf("XCopyPlane case1\n");
#endif

	/*
	 * Case 1: transparent bitmaps are handled by setting the
	 * destination to the foreground color whenever the source
	 * pixel is set.
	 */

	oldColor = GpiQueryColor(destPS);
	oldPattern = GpiQueryPattern(destPS);
	GpiSetColor(destPS, gc->foreground);
	GpiSetPattern(destPS, PATSYM_SOLID);
        rc = GpiBitBlt(destPS, srcPS, 3, aPoints, MASKPAT, BBO_IGNORE);
#ifdef DEBUG
        printf("    GpiBitBlt (clip_mask src) %x, %x returns %d\n", destPS,
               srcPS, rc);
#endif
	GpiSetPattern(destPS, oldPattern);
	GpiSetColor(destPS, oldColor);

    } else if (gc->clip_mask == None) {
#ifdef DEBUG
        printf("XCopyPlane case2\n");
#endif

	/*
	 * Case 2: opaque bitmaps.
	 * Conversion from a monochrome bitmap to a color bitmap/device:
	 *     source 1 -> image foreground color
	 *     source 0 -> image background color
	 */

/*
        oldColor = GpiQueryColor(destPS);
        oldBackColor = GpiQueryBackColor(destPS);
        oldMix = GpiQueryMix(destPS);
        oldBackMix = GpiQueryBackMix(destPS);
#ifdef DEBUG
        printf("    oldColor %d, oldBackColor %d, oldMix %d, oldBackMix %d\n",
               oldColor, oldBackColor, oldMix, oldBackMix);
#endif
*/

/*
        rc= GpiSetColor(destPS, gc->background);
#ifdef DEBUG
        if (rc==TRUE) printf("    GpiSetColor %d OK\n", gc->background);
        else printf("    GpiSetColor %d ERROR: %x\n", gc->background,
                    WinGetLastError(hab));
#endif
        rc= GpiSetBackColor(destPS, gc->foreground);
#ifdef DEBUG
        if (rc==TRUE) printf("    GpiSetBackColor %d OK\n", gc->foreground);
        else printf("    GpiSetBackColor %d ERROR: %x\n", gc->foreground,
                    WinGetLastError(hab));
#endif
*/
        rc= GpiSetColor(destPS, gc->foreground);
#ifdef DEBUG
        if (rc==TRUE) printf("    GpiSetColor %d OK\n", gc->foreground);
        else printf("    GpiSetColor %d ERROR: %x\n", gc->foreground,
                    WinGetLastError(hab));
#endif
        rc= GpiSetBackColor(destPS, gc->background);
#ifdef DEBUG
        if (rc==TRUE) printf("    GpiSetBackColor %d OK\n", gc->background);
        else printf("    GpiSetBackColor %d ERROR: %x\n", gc->background,
                    WinGetLastError(hab));
#endif
        rc= GpiSetMix(destPS, FM_OVERPAINT);
#ifdef DEBUG
        if (rc==TRUE) printf("    GpiSetMix %d OK\n", FM_OVERPAINT);
        else printf("    GpiSetMix %d ERROR: %x\n", FM_OVERPAINT,
                    WinGetLastError(hab));
#endif
        rc= GpiSetBackMix(destPS, BM_OVERPAINT);
#ifdef DEBUG
        if (rc==TRUE) printf("    GpiSetBackMix %d OK\n", BM_OVERPAINT);
        else printf("   GpiSetBackMix %d ERROR: %x\n", BM_OVERPAINT,
                    WinGetLastError(hab));
#endif
        rc = GpiBitBlt(destPS, srcPS, 3, aPoints, ROP_SRCCOPY, BBO_IGNORE);
#ifdef DEBUG
        printf("     GpiBitBlt (clip_mask None) %x -> %x returns %d\n", srcPS,
               destPS, rc);
#endif
/*
        rc= GpiSetColor(destPS, oldColor);
        rc= GpiSetBackColor(destPS, oldBackColor);
        rc= GpiSetMix(destPS, oldMix);
        rc= GpiSetBackMix(destPS, oldBackMix);
*/
    } else {

	/*
	 * Case 3: two arbitrary bitmaps.  Copy the source rectangle
	 * into a color pixmap.  Use the result as a brush when
	 * copying the clip mask into the destination.	 
	 */

	HPS memPS, maskPS;
	BITMAPINFOHEADER2 bmpInfo;
	HBITMAP bitmap, oldBitmap;
	TkOS2PSState maskState;

#ifdef DEBUG
printf("XCopyPlane case3\n");
#endif

	oldColor = GpiQueryColor(destPS);
	oldPattern = GpiQueryPattern(destPS);

	maskPS = TkOS2GetDrawablePS(display, gc->clip_mask, &maskState);
	memPS = WinGetScreenPS(HWND_DESKTOP);
	bmpInfo.cbFix = sizeof(BITMAPINFOHEADER2);
	bmpInfo.cx = width;
	bmpInfo.cy = height;
	bmpInfo.cPlanes = 1;
	bmpInfo.cBitCount = 1;
	bitmap = GpiCreateBitmap(memPS, &bmpInfo, 0L, NULL, NULL);
#ifdef DEBUG
printf("    GpiCreateBitmap (%d,%d) returned %x\n", width, height, bitmap);
#endif
/*
*/
	oldBitmap = GpiSetBitmap(memPS, bitmap);
#ifdef DEBUG
printf("    GpiSetBitmap %x returned %x\n", bitmap, oldBitmap);
#endif
/*
*/

	/*
	 * Set foreground bits.  We create a new bitmap containing
	 * (source AND mask), then use it to set the foreground color
	 * into the destination.
	 */

        /* Translate the Y coordinates to PM coordinates */
        aPoints[0].x = 0; /* dest_x = 0 */
        aPoints[0].y = destWindowHeight - height; /* dest_y = 0 */
        aPoints[1].x = width;
        aPoints[1].y = destWindowHeight;
        aPoints[2].x = src_x;
        aPoints[2].y = srcWindowHeight - src_y - height;
        rc = GpiBitBlt(memPS, srcPS, 3, aPoints, ROP_SRCCOPY, BBO_IGNORE);
#ifdef DEBUG
printf("    GpiBitBlt nr1 %x, %x returns %d\n", destPS, srcPS, rc);
#endif
        /* Translate the Y coordinates to PM coordinates */
        aPoints[0].x = 0; /* dest_x = 0 */
        aPoints[0].y = destWindowHeight - height; /* dest_y = 0 */
        aPoints[1].x = dest_x + width;
        aPoints[1].y = destWindowHeight;
        aPoints[2].x = dest_x - gc->clip_x_origin;
        aPoints[2].y = srcWindowHeight - dest_y + gc->clip_y_origin - height;
        rc = GpiBitBlt(memPS, maskPS, 3, aPoints, ROP_SRCAND, BBO_IGNORE);
#ifdef DEBUG
printf("    GpiBitBlt nr2 %x, %x returns %d\n", destPS, srcPS, rc);
#endif
        /* Translate the Y coordinates to PM coordinates */
        aPoints[0].x = dest_x;
        aPoints[0].y = destWindowHeight - dest_y - height;
        aPoints[1].x = dest_x + width;
        aPoints[1].y = destWindowHeight - dest_y;
        aPoints[2].x = 0; /* src_x = 0 */
        aPoints[2].y = srcWindowHeight - height; /* src_y = 0 */
	GpiSetColor(destPS, gc->foreground);
	GpiSetPattern(destPS, PATSYM_SOLID);
        rc = GpiBitBlt(destPS, memPS, 3, aPoints, MASKPAT, BBO_IGNORE);
#ifdef DEBUG
printf("    GpiBitBlt nr3 %x, %x returns %d\n", destPS, srcPS, rc);
#endif

	/*
	 * Set background bits.  Same as foreground, except we use
	 * ((NOT source) AND mask) and the background brush.
	 */

        /* Translate the Y coordinates to PM coordinates */
        aPoints[0].x = 0; /* dest_x = 0 */
        aPoints[0].y = destWindowHeight - height; /* dest_y = 0 */
        aPoints[1].x = width;
        aPoints[1].y = destWindowHeight;
        aPoints[2].x = src_x;
        aPoints[2].y = srcWindowHeight - src_y - height;
        rc = GpiBitBlt(memPS, srcPS, 3, aPoints, ROP_NOTSRCCOPY, BBO_IGNORE);
#ifdef DEBUG
printf("    GpiBitBlt nr4 %x, %x returns %d\n", destPS, srcPS, rc);
#endif
        /* Translate the Y coordinates to PM coordinates */
        aPoints[0].x = 0; /* dest_x = 0 */
        aPoints[0].y = destWindowHeight - height; /* dest_y = 0 */
        aPoints[1].x = dest_x + width;
        aPoints[1].y = destWindowHeight;
        aPoints[2].x = dest_x - gc->clip_x_origin;
        aPoints[2].y = destWindowHeight - dest_y + gc->clip_y_origin - height;
        rc = GpiBitBlt(memPS, maskPS, 3, aPoints, ROP_SRCAND, BBO_IGNORE);
#ifdef DEBUG
printf("    GpiBitBlt nr5 %x, %x returns %d\n", destPS, srcPS, rc);
#endif
	GpiSetColor(destPS, gc->background);
	GpiSetPattern(destPS, PATSYM_SOLID);
        /* Translate the Y coordinates to PM coordinates */
        aPoints[0].x = dest_x;
        aPoints[0].y = destWindowHeight - dest_y - height;
        aPoints[1].x = dest_x + width;
        aPoints[1].y = destWindowHeight - dest_y;
        aPoints[2].x = 0; /* src_x = 0 */
        aPoints[2].y = srcWindowHeight - height; /* src_y = 0 */
        rc = GpiBitBlt(destPS, memPS, 3, aPoints, MASKPAT, BBO_IGNORE);
#ifdef DEBUG
printf("    GpiBitBlt nr6 %x, %x returns %d\n", destPS, srcPS, rc);
#endif

	TkOS2ReleaseDrawablePS(gc->clip_mask, maskPS, &maskState);
	GpiSetPattern(destPS, oldPattern);
	GpiSetColor(destPS, oldColor);
	GpiSetBitmap(memPS, oldBitmap);
	GpiDeleteBitmap(bitmap);
	WinReleasePS(memPS);
    }
    if (src != dest) {
	TkOS2ReleaseDrawablePS(dest, destPS, &destState);
    }
    TkOS2ReleaseDrawablePS(src, srcPS, &srcState);
}

/*
 *----------------------------------------------------------------------
 *
 * TkPutImage --
 *
 *	Copies a subimage from an in-memory image to a rectangle of
 *	of the specified drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws the image on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
TkPutImage(colors, ncolors, display, d, gc, image, src_x, src_y, dest_x,
	dest_y, width, height)
    unsigned long *colors;		/* Array of pixel values used by this
					 * image.  May be NULL. */
    int ncolors;			/* Number of colors used, or 0. */
    Display* display;
    Drawable d;				/* Destination drawable. */
    GC gc;
    XImage* image;			/* Source image. */
    int src_x, src_y;			/* Offset of subimage. */      
    int dest_x, dest_y;			/* Position of subimage origin in
					 * drawable.  */
    unsigned int width, height;		/* Dimensions of subimage. */
{
    HPS hps;
    HDC dcMem;
    LONG rc;
    TkOS2PSState state;
    BITMAPINFO2 *infoPtr;
    BITMAPINFOHEADER2 bmpInfo;
    HBITMAP bitmap, oldBitmap;
    char *data;
    LONG windowHeight;
    POINTL aPoints[4]; /* Lower-left, upper-right, lower-left source */

#ifdef DEBUG
    printf("TkPutImage d %x, src_x %d, src_y %d, dest_x %d, dest_y %d, w %d, h %d\n",
           d, src_x, src_y, dest_x, dest_y, width, height);
    printf("    nrColors %d, gc->foreground %d, gc->background %d\n", ncolors,
           gc->foreground, gc->background);
#endif

    /* Translate the Y coordinates to PM coordinates */
    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)d);
    aPoints[0].x = dest_x;
    aPoints[0].y = windowHeight - dest_y - height;

    aPoints[1].x = dest_x + width;
    aPoints[1].y = windowHeight - dest_y;

    aPoints[2].x = src_x;
    aPoints[2].y = image->height - src_y - height;

    aPoints[3].x = src_x + width;
    aPoints[3].y = image->height - src_y;
#ifdef DEBUG
printf("    (%d,%d) (%d,%d) (%d,%d) {(%d,%d)}\n", aPoints[0].x, aPoints[0].y,
aPoints[1].x, aPoints[1].y, aPoints[2].x, aPoints[2].y, aPoints[3].x,
aPoints[3].y);
#endif

    display->request++;

    hps = TkOS2GetDrawablePS(display, d, &state);
    rc = GpiSetMix(hps, mixModes[gc->function]);
#ifdef DEBUG
    if (rc == FALSE) {
        printf("    GpiSetMix ERROR, lastError %x\n", WinGetLastError(hab));
    } else {
        printf("    GpiSetMix %x OK\n", mixModes[gc->function]);
    }
    printf("    hps color %d, hps back color %d\n", GpiQueryColor(hps),
           GpiQueryBackColor(hps));
#endif

    if (image->bits_per_pixel == 1) {
#ifdef DEBUG
        printf("image->bits_per_pixel == 1\n");
#endif

        /* Bitmap must be reversed in OS/2 wrt. the Y direction */
        /* This is done best in a modified version of TkAlignImageData */
	data = TkAlignImageData(image, sizeof(ULONG), MSBFirst);
/*
	bmpInfo.cbFix = sizeof(BITMAPINFOHEADER2);
*/
	bmpInfo.cbFix = 16L;
	bmpInfo.cx = image->width;
	bmpInfo.cy = image->height;
	bmpInfo.cPlanes = 1;
	bmpInfo.cBitCount = 1;
	infoPtr = (BITMAPINFO2*) ckalloc(sizeof(BITMAPINFO2));
	if (infoPtr == NULL) {
	    ckfree(data);
	    return;
	}
/*
	infoPtr->cbFix = sizeof(BITMAPINFO2);
*/
	infoPtr->cbFix = 16L;
	infoPtr->cx = image->width;
        infoPtr->cy = image->height;
	infoPtr->cPlanes = 1;
	infoPtr->cBitCount = 1;
        rc = GpiSetBitmapBits(hps, 0L, height, (PBYTE)data, infoPtr);
#ifdef DEBUG
        if (rc == GPI_ALTERROR) {
            SIZEL dim;
            printf("    GpiSetBitmapBits returned GPI_ALTERROR, lastError %x\n",
                   WinGetLastError(hab));
            rc=GpiQueryBitmapDimension(((TkOS2Drawable *)d)->bitmap.handle,
                                       &dim);
            if (rc == FALSE) {
                printf("    GpiQueryBitmapDimension ERROR, lastError %x\n",
                       WinGetLastError(hab));
            } else {
                printf("    GpiQueryBitmapDimension: %dx%d\n", dim.cx, dim.cy);
            }
        } else {
            printf("    GpiSetBitmapBits set %d scanlines\n", rc);
        }
#endif
        /* Move to the destination spot, since GpiSetBitmapBits put it at 0,0 */
/*
        aPoints[2].x = 0;
        aPoints[2].y = 0;
        rc = GpiBitBlt(hps, hps, 3, aPoints, ROP_SRCCOPY, BBO_IGNORE);
#ifdef DEBUG
if (rc==TRUE) printf("Internal GpiBitBlt TRUE\n");
else printf("Internal GpiBitBlt %x ERROR, error %x\n", hps, WinGetLastError(hab));
#endif
        aPoints[2].x = src_x;
        aPoints[2].y = windowHeight - src_y - height;
*/

	ckfree(data);
	ckfree((char *)infoPtr);
    } else {
	int i, usePalette;
	LONG defBitmapFormat[2];

#ifdef DEBUG
printf("image->bits_per_pixel %d\n", image->bits_per_pixel);
#endif

	/*
	 * Do not use a palette for TrueColor images.
	 */
	
	usePalette = (image->bits_per_pixel < 24);

	if (usePalette) {
#ifdef DEBUG
printf("using palette (not TrueColor)\n");
#endif
	    infoPtr = (BITMAPINFO2*) ckalloc(sizeof(BITMAPINFO2)
		    + sizeof(RGB2)*ncolors);
	} else {
#ifdef DEBUG
printf("not using palette (TrueColor)\n");
#endif
	    infoPtr = (BITMAPINFO2*) ckalloc(sizeof(BITMAPINFO2));
	}
	if (infoPtr == NULL) return;

        /* Bitmap must be reversed in OS/2 wrt. the Y direction */
	data = TkOS2ReverseImageLines(image);
	
/*
	infoPtr = (BITMAPINFO2*) ckalloc(sizeof(BITMAPINFO2));
*/
	infoPtr->cbFix = 16L;
	infoPtr->cx = image->width;
        infoPtr->cy = image->height;
	/*
	 * OS/2 doesn't have a Device Independent Bitmap (DIB), so use the
	 * "default" bitmap format (present color depth).
	 */
/*
	rc = GpiQueryDeviceBitmapFormats(hps, 2, defBitmapFormat);
        if (rc != TRUE) {
#ifdef DEBUG
        printf("    GpiQueryDeviceBitmapFormats ERROR %x -> mono\n",
               WinGetLastError(hab));
#endif
	    infoPtr->cPlanes = 1;
	    infoPtr->cBitCount = 1;
        } else {
#ifdef DEBUG
            printf("    GpiQueryDeviceBitmapFormats OK planes %d, bits %d\n",
                   defBitmapFormat[0], defBitmapFormat[1]);
#endif
	    infoPtr->cPlanes = defBitmapFormat[0];
	    infoPtr->cBitCount = defBitmapFormat[1];
        }
*/
	    infoPtr->cPlanes = 1;
	    infoPtr->cBitCount = image->bits_per_pixel;
	/*
	*/
	infoPtr->cbFix = 36;
	infoPtr->ulCompression = BCA_UNCOMP;
	infoPtr->cbImage = 0;
	infoPtr->cxResolution = aDevCaps[CAPS_HORIZONTAL_RESOLUTION];
	infoPtr->cyResolution = aDevCaps[CAPS_VERTICAL_RESOLUTION];
	infoPtr->cclrImportant = 0;

	if (usePalette) {
	    LONG rgb;
	    LONG aClrData[4];
	    LONG alTable[16];
	    BOOL palManager = FALSE;

	    if (aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER) {
	        palManager = TRUE;
#ifdef DEBUG
	        printf("    Palette manager\n");
#endif
	    }
/*
#ifdef DEBUG
rc = GpiCreateLogColorTable(globalPS, 0L, LCOLF_CONSECRGB, 0, 0, NULL);
if (rc!=TRUE) {
    printf("GpiCreateLogColorTable ERROR %x\n", WinGetLastError(hab));
}
#endif
rc = GpiQueryColorData(globalPS, 4, aClrData);
#ifdef DEBUG
if (rc!=TRUE) {
    printf("GpiQueryColorData ERROR %x\n", WinGetLastError(hab));
} else {
    printf("GpiQueryColorData: format %x, loind %d, hiind %d, options %x\n",
           aClrData[QCD_LCT_FORMAT], aClrData[QCD_LCT_LOINDEX],
           aClrData[QCD_LCT_HIINDEX], aClrData[QCD_LCT_OPTIONS]);
}
#endif
*/
	infoPtr->cbFix = sizeof(BITMAPINFO2);
	infoPtr->usUnits = BRU_METRIC;
	infoPtr->usReserved = 0;
	infoPtr->usRecording = BRA_BOTTOMUP;
	infoPtr->usRendering = BRH_NOTHALFTONED;
	infoPtr->cSize1 = 0L;
	infoPtr->cSize2 = 0L;
	infoPtr->ulColorEncoding = BCE_RGB;
	infoPtr->ulIdentifier = 0L;
	/*
	*/
	    infoPtr->cclrUsed = ncolors;
	    for (i = 0; i < ncolors; i++) {
/* Values passed are *indices* */
#ifdef DEBUG
                printf("    index %d\n", colors[i]);
#endif
                if (palManager) {
                    rc = GpiQueryPaletteInfo(GpiQueryPalette(hps), hps, 0,
                                             colors[i], 1L, &rgb);
#ifdef DEBUG
                    if (rc != TRUE) {
                        printf("    GpiQueryPaletteInfo ERROR %x\n",
                               WinGetLastError(hab));
                    } else {
                        printf("    color %d\n", rgb);
                    }
#endif
                } else {
                    rc = GpiQueryLogColorTable(hps, 0, colors[i], 1L, &rgb);
#ifdef DEBUG
                    if (rc == QLCT_ERROR) {
                        printf("    GpiQueryLogColorTable ERROR %x\n",
                               WinGetLastError(hab));
                    } else if (rc == QLCT_RGB) {
                        printf("    table in RGB mode, no element returned");
                    } else {
                        printf("    color %d\n", rgb);
                    }
#endif
                }
		infoPtr->argbColor[i].bRed = GetRValue(rgb);
		infoPtr->argbColor[i].bGreen = GetGValue(rgb);
		infoPtr->argbColor[i].bBlue = GetBValue(rgb);
		infoPtr->argbColor[i].fcOptions = 0;
#ifdef DEBUG
                printf("    colors[%d] %d (%d,%d,%d)\n", i, rgb,
                       infoPtr->argbColor[i].bRed, infoPtr->argbColor[i].bGreen,
                       infoPtr->argbColor[i].bBlue);
#endif
	    }
	} else {
	    infoPtr->cclrUsed = 0;
	}

        rc = GpiSetBitmapBits(hps, 0L, height, (PBYTE)data, infoPtr);
#ifdef DEBUG
if (rc == GPI_ALTERROR) {
printf("GpiSetBitmapBits returned GPI_ALTERROR, lastError %x\n",
WinGetLastError(hab));
} else {
printf("GpiSetBitmapBits set %d scanlines\n", rc);
}
#endif

	ckfree((char *)infoPtr);
	ckfree(data);
    }
    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawString --
 *
 *	Draw a single string in the current font.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the specified string in the drawable.
 *
 *----------------------------------------------------------------------
 */

void
XDrawString(display, d, gc, x, y, string, length)
    Display* display;
    Drawable d;
    GC gc;
    int x;
    int y;
    _Xconst char* string;
    int length;
{
    HPS hps;
    SIZEF oldCharBox;
    LONG oldFont;
    LONG oldHorAlign, oldVerAlign;
    LONG oldBackMix;
    LONG oldPalette;
    LONG oldColor, oldBackColor;
    POINTL oldRefPoint;
    LONG oldPattern;
    LONG oldPatternSet;
    HBITMAP oldBitmap;
    POINTL noShear= {0, 1};
    POINTL aSize[TXTBOX_COUNT];
    TkOS2PSState state;
    POINTL aPoints[3]; /* Lower-left, upper-right, lower-left source */
    CHARBUNDLE cBundle, oldCharBundle;
    LONG windowHeight, l;
    char *str;
    FONTMETRICS fm;
    POINTL refPoint;
    BOOL rc;

    display->request++;

    if (d == None) {
	return;
    }

#ifdef DEBUG
printf("XDrawString \"%s\" (%d) on type %s (%x), mixmode %d\n", string, length,
((TkOS2Drawable *)d)->type == TOD_BITMAP ? "bitmap" : "window", d,
mixModes[gc->function]);
#endif
    hps = TkOS2GetDrawablePS(display, d, &state);
    GpiSetMix(hps, mixModes[gc->function]);

    /* If this is an outline font, set the char box */
    if (logfonts[(LONG)gc->font].outline) {
        SIZEF charBox;
        rc = GpiQueryCharBox(hps, &oldCharBox);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiQueryCharBox ERROR %x\n", WinGetLastError(hab));
else printf("GpiQueryCharBox OK: cx %d (%d,%d), cy %d (%d,%d)\n", oldCharBox.cx,
            FIXEDINT(oldCharBox.cx), FIXEDFRAC(oldCharBox.cx), oldCharBox.cy,
            FIXEDINT(oldCharBox.cy), FIXEDFRAC(oldCharBox.cy));
#endif
        rc = TkOS2ScaleFont(hps, logfonts[(LONG)gc->font].pixelSize, 0);
#ifdef DEBUG
if (rc!=TRUE) printf("TkOS2ScaleFont %d ERROR %x\n",
                     logfonts[(LONG)gc->font].pixelSize, WinGetLastError(hab));
else printf("TkOS2ScaleFont %d OK\n", logfonts[(LONG)gc->font].pixelSize);
        rc = GpiQueryCharBox(hps, &charBox);
if (rc!=TRUE) printf("GpiQueryCharBox ERROR %x\n");
else printf("GpiQueryCharBox OK: now cx %d (%d,%d), cy %d (%d,%d)\n", charBox.cx,
            FIXEDINT(charBox.cx), FIXEDFRAC(charBox.cx), charBox.cy,
            FIXEDINT(charBox.cy), FIXEDFRAC(charBox.cy));
#endif
    }

    /* Translate the Y coordinates to PM coordinates */
    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)d);
#ifdef DEBUG
printf("    x %d, y %d (PM: %d)\n", x, y, windowHeight - y);
#endif
    y = windowHeight - y;

    if ((gc->fill_style == FillStippled
	    || gc->fill_style == FillOpaqueStippled)
	    && gc->stipple != None) {
	TkOS2Drawable *todPtr = (TkOS2Drawable *)gc->stipple;
	HBITMAP bitmap;
        BITMAPINFOHEADER2 bmpInfo;
        HRGN hrgn;
        RECTL rect;
SWP pos;
POINTL point;

#ifdef DEBUG
printf("XDrawString stippled \"%s\" (%x) at %d,%d; fg %d, bg %d, bmp %x\n",
string, gc->stipple, x, y, gc->foreground, gc->background, todPtr->bitmap.handle);
#endif

	if (todPtr->type != TOD_BITMAP) {
	    panic("unexpected drawable type in stipple");
	}
	refPoint.x = 0;
	refPoint.y = 0;

	if (gc->font != None) {
            rc = GpiCreateLogFont(hps, NULL, (LONG)gc->font,
                                  &(logfonts[(LONG)gc->font].fattrs));
#ifdef DEBUG
if (rc!=GPI_ERROR) printf("GpiCreateLogFont (%x, id %d) OK\n", hps, gc->font);
else printf("GpiCreateLogFont (%x, id %d) ERROR, error %x\n", hps,
gc->font, WinGetLastError(hab));
#endif
	    oldFont = GpiQueryCharSet(hps);
#ifdef DEBUG
if (rc==LCID_ERROR) printf("GpiQueryCharSet ERROR %x\n", WinGetLastError(hab));
else printf("GpiQueryCharSet OK\n");
#endif
	    rc = GpiSetCharSet(hps, (LONG)gc->font);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiSetCharSet (%x, id %d, face [%s]) ERROR, error %x\n",
hps, gc->font, logfonts[(LONG)gc->font].fattrs.szFacename, WinGetLastError(hab));
else printf("GpiSetCharSet (%x, id %d, face [%s]) OK\n", hps, gc->font,
logfonts[(LONG)gc->font].fattrs.szFacename);
#endif
	    /* Set slant if necessary */
	    if (logfonts[(LONG)gc->font].setShear) {
	        rc = GpiSetCharShear(hps, &(logfonts[(LONG)gc->font].shear));
#ifdef DEBUG
if (rc!=TRUE) printf("GpiSetCharShear ERROR %x\n", WinGetLastError(hab));
else printf("GpiSetCharShear OK\n");
#endif
	    }
	}

	/*
	 * Compute the bounding box and create a compatible bitmap.
	 */

        rc = GpiQueryFontMetrics(hps, sizeof(FONTMETRICS), &fm);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiQueryFontMetrics ERROR %x\n", WinGetLastError(hab));
else printf("GpiQueryFontMetrics OK\n");
#endif
    /* If this is an outline font, set the char box */
    if (logfonts[(LONG)gc->font].outline) {
        SIZEF charBox;
        rc = TkOS2ScaleFont(hps, logfonts[(LONG)gc->font].pixelSize, 0);
#ifdef DEBUG
if (rc!=TRUE) printf("TkOS2ScaleFont %d ERROR %x\n",
                     logfonts[(LONG)gc->font].pixelSize,
                     WinGetLastError(hab));
else printf("TkOS2ScaleFont %d OK\n", logfonts[(LONG)gc->font].pixelSize);
#endif
        rc = GpiQueryCharBox(hps, &charBox);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiQueryCharBox ERROR %x\n");
else printf("GpiQueryCharBox OK: now cx %d (%d,%d), cy %d (%d,%d)\n", charBox.cx,
            FIXEDINT(charBox.cx), FIXEDFRAC(charBox.cx), charBox.cy,
            FIXEDINT(charBox.cy), FIXEDFRAC(charBox.cy));
#endif
    }
	GpiQueryTextBox(hps, length, (PCH)string, TXTBOX_COUNT,
	                aSize);
	/* OS/2 PM does not have this overhang
	 * aSize[TXTBOX_CONCAT].cx -= fm.tmOverhang;
	bmpInfo.cbFix = sizeof(BITMAPINFOHEADER2);
	 */

	refPoint.x = point.x + gc->ts_x_origin;
	/* Translate Xlib y to PM y */
	refPoint.y = point.y + windowHeight - gc->ts_y_origin;
#ifdef DEBUG
printf("gc->ts_x_origin=%d (->%d), gc->ts_y_origin=%d (->%d)\n", gc->ts_x_origin,
refPoint.x, gc->ts_y_origin, refPoint.y);
#endif
	rc = GpiQueryPatternRefPoint(hps, &oldRefPoint);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiQueryPatternRefPoint ERROR %x\n", WinGetLastError(hab));
else printf("GpiQueryPatternRefPoint OK\n");
#endif
	rc = GpiSetPatternRefPoint(hps, &refPoint);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiSetPatternRefPoint ERROR %x\n", WinGetLastError(hab));
else printf("GpiSetPatternRefPoint OK\n");
#endif
	oldPattern = GpiQueryPatternSet(hps);
#ifdef DEBUG
if (rc==LCID_ERROR) printf("GpiQueryPatternSet ERROR %x\n", WinGetLastError(hab));
else printf("GpiQueryPatternSet %x\n", oldPattern);
#endif
	/* Local ID 254 to keep from clashing with fonts as long as possible */
/*
	rc = GpiSetBitmapId(hps, todPtr->bitmap.handle, 254L);
#ifdef DEBUG
        if (rc!=TRUE) printf("GpiSetBitmapId ERROR %x\n", WinGetLastError(hab));
        else printf("GpiSetBitmapId OK\n");
#endif
        rc = GpiSetPatternSet(hps, 254L);
#ifdef DEBUG
        if (rc!=TRUE) printf("GpiSetPatternSet ERROR %x\n", WinGetLastError(hab));
        else printf("GpiSetPatternSet OK\n");
#endif
*/
/* The bitmap mustn't be selected in the HPS */
TkOS2SetStipple(todPtr->bitmap.hps, todPtr->bitmap.hps, todPtr->bitmap.handle,
refPoint.x, refPoint.y, &oldPattern, &oldRefPoint);

	oldBackMix = GpiQueryBackMix(hps);
#ifdef DEBUG
if (rc==BM_ERROR) printf("GpiQueryBackMix ERROR %x\n", WinGetLastError(hab));
else printf("GpiQueryBackMix OK\n");
#endif
	rc = GpiSetBackMix(hps, BM_LEAVEALONE);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiSetBackMix ERROR %x\n", WinGetLastError(hab));
else printf("GpiSetBackMix OK\n");
#endif

	/*
	 * The following code is tricky because fonts are rendered in multiple
	 * colors.  First we draw onto a black background and copy the white
	 * bits.  Then we draw onto a white background and copy the black bits.
	 * Both the foreground and background bits of the font are ANDed with
	 * the stipple pattern as they are copied.
	 */

	rect.xLeft = 0L;
	rect.yBottom = windowHeight - aSize[TXTBOX_BOTTOMRIGHT].y;
	rect.xRight = aSize[TXTBOX_CONCAT].x;
	rect.yTop = windowHeight;
        rc= WinFillRect(hps, &rect, CLR_TRUE);
#ifdef DEBUG
if (rc!=TRUE) printf("WinFillRect %d,%d->%d,%d ERROR %x\n",
rect.xLeft, rect.yBottom, rect.xRight, rect.yTop, WinGetLastError(hab));
else printf("WinFillRect %d,%d->%d,%d OK\n",
rect.xLeft, rect.yBottom, rect.xRight, rect.yTop);
#endif
        oldColor = GpiQueryColor(hps);
#ifdef DEBUG
if (rc==CLR_ERROR) printf("GpiQueryColor ERROR %x\n", WinGetLastError(hab));
else printf("GpiQueryColor OK\n");
#endif
	rc = GpiSetColor(hps, CLR_TRUE);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiSetColor ERROR %x\n", WinGetLastError(hab));
else printf("GpiSetColor OK\n");
#endif
	rc = GpiSetColor(hps, oldColor);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiSetColor ERROR %x\n", WinGetLastError(hab));
else printf("GpiSetColor OK\n");
#endif

	cBundle.lColor = gc->foreground;
	rc = GpiSetAttrs(hps, PRIM_CHAR, LBB_COLOR, 0L, (PBUNDLE)&cBundle);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiSetAttrs ERROR %x\n", WinGetLastError(hab));
else printf("GpiSetAttrs OK\n");
#endif
	refPoint.y = windowHeight -
	             (aSize[TXTBOX_TOPRIGHT].y - aSize[TXTBOX_BOTTOMRIGHT].y);
        /* only 512 bytes allowed in string */
        rc = GpiMove(hps, &refPoint);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiMove %d,%d ERROR %x\n", refPoint.x, refPoint.y, WinGetLastError(hab));
else printf("GpiMove %d,%d OK\n", refPoint.x, refPoint.y);
#endif
        l = length;
        str = (char *)string;
        while (length>512) {
            rc = GpiCharString(hps, l, (PCH)str);
#ifdef DEBUG
if (rc==GPI_ERROR) printf("GpiCharString ERROR %x\n", WinGetLastError(hab));
else printf("GpiCharString OK\n");
#endif
            l -= 512;
            str += 512;
        }
        rc = GpiCharString(hps, l, (PCH)str);
#ifdef DEBUG
if (rc==GPI_ERROR) printf("GpiCharString ERROR %x\n", WinGetLastError(hab));
else printf("GpiCharString OK\n");
#endif

        /* Translate the Y coordinates to PM coordinates */
        aPoints[0].x = x;
        aPoints[0].y = y;
/*
        aPoints[1].x = x + aSize[TXTBOX_CONCAT].x
                         - aSize[TXTBOX_BOTTOMLEFT].x;
        aPoints[1].y = y +
                       (aSize[TXTBOX_TOPRIGHT].y - aSize[TXTBOX_BOTTOMRIGHT].y);
*/
        aPoints[1].x = rect.xRight;
	aPoints[1].y = rect.yTop;
        aPoints[2].x = 0;
	aPoints[2].y = 0;
/*
        aPoints[2].y = windowHeight
                       - (aSize[TXTBOX_TOPLEFT].y - aSize[TXTBOX_BOTTOMLEFT].y);
*/
/*
#ifdef DEBUG
printf("aPoints: %d,%d %d,%d <- %d,%d\n", aPoints[0].x, aPoints[0].y,
aPoints[1].x, aPoints[1].y, aPoints[2].x, aPoints[2].y);
#endif
        rc = GpiBitBlt(hps, todPtr->bitmap.hps, 3, aPoints, (LONG)0x00ea, BBO_IGNORE);
#ifdef DEBUG
if (rc==GPI_ERROR) printf("GpiBitBlt ERROR %x\n", WinGetLastError(hab));
else printf("GpiBitBlt OK\n");
#endif
*/
	rc = GpiSetColor(hps, CLR_FALSE);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiSetColor ERROR %x\n", WinGetLastError(hab));
else printf("GpiSetColor OK\n");
#endif
	rc = GpiSetColor(hps, oldColor);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiSetColor ERROR %x\n", WinGetLastError(hab));
else printf("GpiSetColor OK\n");
#endif

	cBundle.lColor = gc->foreground;
	rc = GpiSetAttrs(hps, PRIM_CHAR, LBB_COLOR, 0L, (PBUNDLE)&cBundle);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiSetAttrs ERROR %x\n", WinGetLastError(hab));
else printf("GpiSetAttrs OK\n");
#endif

        /* only 512 bytes allowed in string */
        rc = GpiMove(hps, &refPoint);
#ifdef DEBUG
if (rc!=TRUE) printf("GpiMove %d, %d ERROR %x\n", refPoint.x, refPoint.y, WinGetLastError(hab));
else printf("GpiMove %d,%d OK\n", refPoint.x, refPoint.y);
#endif
        l = length;
        str = (char *)string;
        while (length>512) {
            rc = GpiCharString(hps, 512, (PCH)str);
#ifdef DEBUG
if (rc==GPI_ERROR) printf("GpiCharString ERROR %x\n", WinGetLastError(hab));
else printf("GpiCharString OK\n");
#endif
            l -= 512;
            str += 512;
        }
        rc = GpiCharString(hps, l, (PCH)str);
#ifdef DEBUG
if (rc==GPI_ERROR) printf("GpiCharString ERROR %x\n", WinGetLastError(hab));
else printf("GpiCharString OK\n");
#endif
/*
        rc = GpiBitBlt(hps, todPtr->bitmap.hps, 3, aPoints, (LONG)0x008a, BBO_IGNORE);
#ifdef DEBUG
if (rc==GPI_ERROR) printf("GpiBitBlt ERROR %x\n", WinGetLastError(hab));
else printf("GpiBitBlt OK\n");
#endif
*/

	/*
	 * Destroy the temporary bitmap and restore the device context.
	 */

	rc = GpiSetColor(hps, oldColor);
	if (gc->font != None) {
	    /* Set slant if necessary */
	    if (logfonts[(LONG)gc->font].setShear) {
	        rc = GpiSetCharShear(hps, &noShear);
	    }
	    rc = GpiSetCharSet(hps, oldFont);
	}
	rc = GpiSetBackMix(hps, oldBackMix);
	cBundle.lColor = oldColor;
	rc = GpiSetAttrs(hps, PRIM_CHAR, LBB_COLOR, 0L, (PBUNDLE)&cBundle);
	rc = GpiSetPatternSet(hps, oldPattern);
	rc = GpiSetPatternRefPoint(hps, &oldRefPoint);
	rc = GpiDeleteSetId(hps, 254L);
	/* end of using 254 */
/* The bitmap must be reselected in the HPS */
TkOS2UnsetStipple(todPtr->bitmap.hps, todPtr->bitmap.hps, todPtr->bitmap.handle,
                  oldPattern, &oldRefPoint);

    } else {
        TkOS2Drawable *todPtr = (TkOS2Drawable *)d;
        ULONG changed;
	RECTL allRect;

	/* Select appropriate palette into hps */
/*
        if (todPtr->type != TOD_BITMAP) {
            if (todPtr->window.winPtr == NULL) {
#ifdef DEBUG
printf("XDrawString at %d,%d; cmap DEF fg %d, bg %d; window %x\n",
x, y, gc->foreground, gc->background, todPtr->window.handle);
#endif
                oldPalette = TkOS2SelectPalette(hps, todPtr->window.handle,
                              DefaultColormap(display, DefaultScreen(display)));
            } else {
                oldPalette = TkOS2SelectPalette(hps, todPtr->window.handle,
                                          todPtr->window.winPtr->atts.colormap);
#ifdef DEBUG
printf("XDrawString at %d,%d; cmap %x fg %d, bg %d; window %x\n",
x, y, todPtr->window.winPtr->atts.colormap, gc->foreground,
gc->background, todPtr->window.handle);
#endif
            }
        } else {
            oldPalette = TkOS2SelectPalette(hps, todPtr->bitmap.parent,
                                            todPtr->bitmap.colormap);
        }
#ifdef DEBUG
printf("          oldPalette %x (now %x), changed %d\n", oldPalette,
GpiQueryPalette(hps), changed);
#endif
*/

	GpiQueryTextAlignment(hps, &oldHorAlign, &oldVerAlign);
	GpiSetTextAlignment(hps, TA_LEFT, TA_BASE);

	GpiQueryAttrs(hps, PRIM_CHAR, LBB_COLOR, (PBUNDLE)&cBundle);
	oldColor = cBundle.lColor;
	cBundle.lColor = gc->foreground;
	GpiSetAttrs(hps, PRIM_CHAR, LBB_COLOR, 0L, (PBUNDLE)&cBundle);
/*
	oldBackColor = GpiQueryBackColor(hps);
	GpiSetBackColor(hps, CLR_FALSE);
*/

	oldBackMix = GpiQueryBackMix(hps);
	/* We get a crash in PMMERGE.DLL on anything other than BM_LEAVEALONE */
	GpiSetBackMix(hps, BM_LEAVEALONE);

        rc = GpiCreateLogFont(hps, NULL, (LONG)gc->font,
                              &(logfonts[(LONG)gc->font].fattrs));
#ifdef DEBUG
if (rc!=GPI_ERROR) printf("GpiCreateLogFont (%x, id %d) OK\n", hps, gc->font);
else printf("GpiCreateLogFont (%x, id %d) ERROR, error %x\n", hps,
gc->font, WinGetLastError(hab));
#endif
	if (gc->font != None) {
	    oldFont = GpiQueryCharSet(hps);
	    rc = GpiSetCharSet(hps, (LONG)gc->font);
#ifdef DEBUG
if (rc==TRUE) printf("GpiSetCharSet (%x, id %d, %s) OK\n", hps, gc->font,
logfonts[(LONG)gc->font].fattrs.szFacename);
else printf("GpiSetCharSet (%x, id %d, %s) ERROR, error %x\n", hps,
gc->font, logfonts[(LONG)gc->font].fattrs.szFacename, WinGetLastError(hab));
#endif
	    /* Set slant if necessary */
	    if (logfonts[(LONG)gc->font].setShear) {
	        GpiSetCharShear(hps, &(logfonts[(LONG)gc->font].shear));
	    }
	}

	refPoint.x = x;
	refPoint.y = y;
        /* only 512 bytes allowed in string */
        rc = GpiMove(hps, &refPoint);
#ifdef DEBUG
if (rc==TRUE) printf("GpiMove %d,%d OK\n", refPoint.x, refPoint.y);
else printf("GpiMove %d,%d ERROR %x\n", refPoint.x, refPoint.y,
WinGetLastError(hab));
#endif
        l = length;
        str = (char *)string;
        while (l>512) {
            rc = GpiCharString(hps, 512, (PCH)str);
#ifdef DEBUG
if (rc==GPI_OK) printf("GpiCharString returns GPI_OK\n");
else printf("GpiCharString returns %d, ERROR %x\n", rc, WinGetLastError(hab));
#endif
            l -= 512;
            str += 512;
        }
        rc = GpiCharString(hps, l, (PCH)str);
#ifdef DEBUG
if (rc==GPI_OK) printf("GpiCharString returns GPI_OK\n");
else printf("GpiCharString returns %d, ERROR %x\n", rc, WinGetLastError(hab));
#endif

	/* Blit to window's PS */
        /* Translate the Y coordinates to PM coordinates */
/*
        GpiQueryFontMetrics(hps, sizeof(FONTMETRICS), &fm);
*/
        aPoints[0].x = x;
        aPoints[0].y = y - logfonts[gc->font].fm.lMaxDescender;
        rc = GpiQueryCurrentPosition(hps, &refPoint);
#ifdef DEBUG
printf("GpiQueryCurrentPosition returns %d (%d,%d)\n", rc, refPoint.x,
refPoint.y);
#endif
    /* If this is an outline font, set the char box */
    if (logfonts[(LONG)gc->font].outline) {
        SIZEF charBox;
        rc = TkOS2ScaleFont(hps, logfonts[(LONG)gc->font].pixelSize, 0);
#ifdef DEBUG
if (rc!=TRUE) printf("TkOS2ScaleFont %d ERROR %x\n",
                     logfonts[(LONG)gc->font].pixelSize, WinGetLastError(hab));
else printf("TkOS2ScaleFont %d OK\n", logfonts[(LONG)gc->font].pixelSize);
        rc = GpiQueryCharBox(hps, &charBox);
if (rc!=TRUE) printf("GpiQueryCharBox ERROR %x\n");
else printf("GpiQueryCharBox OK: now cx %d (%d,%d), cy %d (%d,%d)\n", charBox.cx,
            FIXEDINT(charBox.cx), FIXEDFRAC(charBox.cx), charBox.cy,
            FIXEDINT(charBox.cy), FIXEDFRAC(charBox.cy));
#endif
    }
	GpiQueryTextBox(hps, length, (PCH)string, TXTBOX_COUNT, aSize);
        aPoints[1].x = refPoint.x;
        aPoints[1].y = y - logfonts[gc->font].fm.lMaxDescender +
                       (aSize[TXTBOX_TOPRIGHT].y - aSize[TXTBOX_BOTTOMRIGHT].y);
        aPoints[2].x = x;
        aPoints[2].y = y - logfonts[gc->font].fm.lMaxDescender;
	allRect.xLeft = aPoints[0].x;
	allRect.yBottom = aPoints[0].y;
	allRect.xRight = aPoints[1].x;
	allRect.yTop = aPoints[1].y;

        rc = GpiSetCharSet(hps, LCID_DEFAULT);
#ifdef DEBUG
if (rc==TRUE) printf("GpiSetCharSet (%x, default) OK\n", hps);
else printf("GpiSetCharSet (%x, default) ERROR, error %x\n", hps,
WinGetLastError(hab));
#endif
        rc = GpiDeleteSetId(hps, (LONG)gc->font);
#ifdef DEBUG
if (rc==TRUE) printf("GpiDeleteSetId (%x, id %d) OK\n", hps, gc->font);
else printf("GpiDeleteSetId (%x, id %d) ERROR, error %x\n", hps,
gc->font, WinGetLastError(hab));
#endif


        /* restore appopriate palette into hps */
/*
        GpiSelectPalette(hps, oldPalette);
*/

	if (gc->font != None) {
	    /* Set slant if necessary */
	    if (logfonts[(LONG)gc->font].setShear) {
	        GpiSetCharShear(hps, &noShear);
	    }
	    GpiSetCharSet(hps, oldFont);
	}
	GpiSetBackMix(hps, oldBackMix);
	GpiSetBackColor(hps, oldBackColor);
	cBundle.lColor = oldColor;
	GpiSetAttrs(hps, PRIM_CHAR, LBB_COLOR, 0L, (PBUNDLE)&cBundle);
	GpiSetTextAlignment(hps, oldHorAlign, oldVerAlign);
    }

    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * XFillRectangles --
 *
 *	Fill multiple rectangular areas in the given drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws onto the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XFillRectangles(display, d, gc, rectangles, nrectangles)
    Display* display;
    Drawable d;
    GC gc;
    XRectangle* rectangles;
    int nrectangles;
{
    HPS hps;
    int i;
    RECTL rect;
    TkOS2PSState state;
    LONG windowHeight;
    POINTL refPoint;
    LONG oldPattern, oldPalette, oldBitmap, oldMix;
    ULONG changed;
    TkOS2Drawable *todPtr = (TkOS2Drawable *)d;
    LONG rc;

    if (d == None) {
	return;
    }
    if (todPtr->type == TOD_BITMAP) {
#ifdef DEBUG
printf("XFillRectangles bitmap %x, handle %x, colormap %x, depth %d\n", todPtr,
todPtr->bitmap.handle, todPtr->bitmap.colormap, todPtr->bitmap.depth);
#endif
    } else {
#ifdef DEBUG
printf("XFillRectangles todPtr %x, winPtr %x, handle %x\n", todPtr,
todPtr->window.winPtr, todPtr->window.handle);
#endif
    }
/*
*/

    windowHeight = TkOS2WindowHeight(todPtr);

    hps = TkOS2GetDrawablePS(display, d, &state);
    GpiSetMix(hps, mixModes[gc->function]);

    if ((gc->fill_style == FillStippled
	    || gc->fill_style == FillOpaqueStippled)
	    && gc->stipple != None) {
	HBITMAP bitmap;
        BITMAPINFOHEADER2 bmpInfo;
        LONG rc;
        DEVOPENSTRUC dop = {0L, (PSZ)"DISPLAY", NULL, 0L, 0L, 0L, 0L, 0L, 0L};
        SIZEL sizl = {0,0}; /* use same page size as device */
        HDC dcMem;
	HPS psMem;
        POINTL aPoints[3]; /* Lower-left, upper-right, lower-left source */

#ifdef DEBUG
printf("                stippled\n");
#endif
	todPtr = (TkOS2Drawable *)gc->stipple;

	if (todPtr->type != TOD_BITMAP) {
	    panic("unexpected drawable type in stipple");
	}

	/*
	 * Select stipple pattern into destination dc.
	 */

	psMem = WinGetScreenPS(HWND_DESKTOP);
/*
*/
	refPoint.x = gc->ts_x_origin;
	/* Translate Xlib y to PM y */
	refPoint.y = windowHeight - gc->ts_y_origin;
	GpiSetPatternRefPoint(hps, &refPoint);
	oldPattern = GpiQueryPatternSet(hps);
	/* Local ID 254 to keep from clashing with fonts as long as possible */
	rc = GpiSetBitmapId(hps, todPtr->bitmap.handle, 254);
	GpiSetPatternSet(hps, 254L);
        oldPalette = GpiSelectPalette(hps, todPtr->bitmap.colormap);
        WinRealizePalette(todPtr->bitmap.parent, hps, &changed);

	dcMem = DevOpenDC(hab, OD_MEMORY, (PSZ)"*", 5L, (PDEVOPENDATA)&dop,
	                  NULLHANDLE);
	if (dcMem == DEV_ERROR) {
#ifdef DEBUG
printf("DevOpenDC ERROR in XFillRectangles\n");
#endif
	    return;
	}
#ifdef DEBUG
printf("DevOpenDC in XFillRectangles returns %x\n", dcMem);
#endif
        psMem = GpiCreatePS(hab, dcMem, &sizl,
                            PU_PELS | GPIT_NORMAL | GPIA_ASSOC);
        if (psMem == GPI_ERROR) {
#ifdef DEBUG
printf("GpiCreatePS ERROR in XFillRectangles: %x\n", WinGetLastError(hab));
#endif
            DevCloseDC(dcMem);
            return;
        }
#ifdef DEBUG
printf("GpiCreatePS in XFillRectangles returns %x\n", psMem);
#endif

	/*
	 * For each rectangle, create a drawing surface which is the size of
	 * the rectangle and fill it with the background color.  Then merge the
	 * result with the stipple pattern.
	 */

	for (i = 0; i < nrectangles; i++) {
/*
	    bmpInfo.cbFix = sizeof(BITMAPINFOHEADER2);
*/
	    bmpInfo.cbFix = 16L;
	    bmpInfo.cx = rectangles[i].width;
	    bmpInfo.cy = rectangles[i].height;
	    bmpInfo.cPlanes = 1;
	    bmpInfo.cBitCount = 1;
	    bitmap = GpiCreateBitmap(psMem, &bmpInfo, 0L, NULL, NULL);
#ifdef DEBUG
if (bitmap == GPI_ERROR) {
printf("GpiCreateBitmap (%d,%d) GPI_ERROR %x\n", bmpInfo.cx, bmpInfo.cy,
WinGetLastError(hab));
} else {
printf("GpiCreateBitmap (%d,%d) returned %x\n", bmpInfo.cx, bmpInfo.cy, bitmap);
}
#endif
	    oldBitmap = GpiSetBitmap(psMem, bitmap);
#ifdef DEBUG
if (bitmap == HBM_ERROR) {
printf("GpiSetBitmap (%x) HBM_ERROR %x\n", bitmap, WinGetLastError(hab));
} else {
printf("GpiSetBitmap %x returned %x\n", bitmap, oldBitmap);
}
#endif
            /* Translate the Y coordinates to PM coordinates */
	    rect.xLeft = 0;
	    rect.yBottom = windowHeight - rectangles[i].height;
	    rect.xRight = rectangles[i].width;
	    rect.yTop = windowHeight;
/*
rc = WinDrawBitmap(hps, todPtr->bitmap.handle, NULL, (PPOINTL)&rect,
gc->foreground, gc->background, DBM_STRETCH);
#ifdef DEBUG
if (rc == FALSE) {
printf("WinDrawBitmap fg %d, bg %d ERROR %x\n", gc->foreground, gc->background,
WinGetLastError(hab));
} else {
printf("WinDrawBitmap fg %d, bg %d OK\n", gc->foreground, gc->background);
}
#endif
*/
	    oldPattern = GpiQueryPattern(psMem);
	    GpiSetPattern(psMem, PATSYM_SOLID);
	    WinFillRect(psMem, &rect, gc->foreground);
#ifdef DEBUG
printf("WinFillRect3\n");
#endif
            /* Translate the Y coordinates to PM coordinates */
            aPoints[0].x = rectangles[i].x;
            aPoints[0].y = windowHeight - rectangles[i].y
                            - rectangles[i].height;
            aPoints[1].x = rectangles[i].x + rectangles[i].width;
            aPoints[1].y = windowHeight - rectangles[i].y;
            aPoints[2].x = aPoints[0].x;
            aPoints[2].y = aPoints[0].y;
            GpiBitBlt(hps, psMem, 3, aPoints, COPYFG, BBO_IGNORE);
	    if (gc->fill_style == FillOpaqueStippled) {
	        WinFillRect(psMem, &rect, gc->background);
#ifdef DEBUG
printf("WinFillRect4\n");
#endif
                GpiBitBlt(hps, psMem, 3, aPoints, COPYBG, BBO_IGNORE);
	    }
	    GpiSetPattern(psMem, oldPattern);
	    GpiDeleteBitmap(bitmap);
	}
        DevCloseDC(dcMem);
	GpiDestroyPS(psMem);
	WinReleasePS(psMem);
        GpiSelectPalette(hps, oldPalette);
	GpiSetPatternSet(hps, oldPattern);
	GpiDeleteSetId(hps, 254L);
	/* end of using 254 */
    } else {

        if (todPtr->type == TOD_BITMAP) {
            oldPalette = TkOS2SelectPalette(hps, HWND_DESKTOP,
                                            todPtr->bitmap.colormap);
        } else {
            if (todPtr->window.winPtr == NULL) {
                oldPalette = TkOS2SelectPalette(hps, todPtr->window.handle,
                             DefaultColormap(display, DefaultScreen(display)));
            } else {
                oldPalette = TkOS2SelectPalette(hps, todPtr->window.handle,
                                         todPtr->window.winPtr->atts.colormap);
            }
        }

        for (i = 0; i < nrectangles; i++) {
            rect.xLeft = rectangles[i].x;
            rect.xRight = rect.xLeft + rectangles[i].width;
            rect.yTop = windowHeight - rectangles[i].y;
            rect.yBottom = rect.yTop - rectangles[i].height;
/*
#ifdef DEBUG
printf("     window %x: xL %d, xR %d, yT %d, yB %d\n", todPtr->window.handle,
rect.xLeft, rect.xRight, rect.yTop, rect.yBottom);
#endif
*/
            rc = WinFillRect(hps, &rect, gc->foreground);
#ifdef DEBUG
if (rc==TRUE) printf("    WinFillRect5(hps %x, fore %d, (%d,%d)(%d,%d)) OK\n",
hps, gc->foreground, rect.xLeft, rect.yBottom, rect.xRight, rect.yTop);
else printf("    WinFillRect5(hps %x, fore %d, (%d,%d)(%d,%d)) ERROR, error %x\n",
hps, gc->foreground, rect.xLeft, rect.yBottom, rect.xRight, rect.yTop,
WinGetLastError(hab));
#endif
            GpiSetPattern(hps, oldPattern);
        }
    }

    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * RenderObject --
 *
 *	This function draws a shape using a list of points, a
 *	stipple pattern, and the specified drawing function.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
RenderObject(hps, gc, d, points, npoints, mode, lineBundle, func)
    HPS hps;
    GC gc;
    Drawable d;
    XPoint* points;
    int npoints;
    int mode;
    PLINEBUNDLE lineBundle;
    int func;
{
    RECTL rect;
    LINEBUNDLE oldLineBundle;
    LONG oldPattern;
    LONG oldColor;
    POINTL oldRefPoint;
    /* os2Points/rect get *PM* coordinates handed to it by ConvertPoints */
    POINTL *os2Points = ConvertPoints(d, points, npoints, mode, &rect);
    POINTL refPoint;
    POINTL aPoints[3]; /* Lower-left, upper-right, lower-left source */
    LONG windowHeight;
    POLYGON polygon;

    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)d);

    if ((gc->fill_style == FillStippled
	    || gc->fill_style == FillOpaqueStippled)
	    && gc->stipple != None) {

	TkOS2Drawable *todPtr = (TkOS2Drawable *)gc->stipple;
	HPS psMem;
	LONG width, height;
	int i;
	HBITMAP bitmap, oldBitmap;
        BITMAPINFOHEADER2 bmpInfo;

#ifdef DEBUG
printf("RenderObject stippled (%x)\n", todPtr->bitmap.handle);
#endif
	
	if (todPtr->type != TOD_BITMAP) {
	    panic("unexpected drawable type in stipple");
	}

    
	width = rect.xRight - rect.xLeft;
	/* PM coordinates are just reverse: top - bottom */
	height = rect.yTop - rect.yBottom;

	/*
	 * Select stipple pattern into destination hps.
	 */
	
	refPoint.x = gc->ts_x_origin;
	/* Translate Xlib y to PM y */
	refPoint.y = windowHeight - gc->ts_y_origin;
/*
	rc = GpiSetPatternRefPoint(hps, &refPoint);
#ifdef DEBUG
if (rc!=TRUE) printf("    GpiSetPatternRefPoint %d,%d ERROR %x\n", refPoint.x,
refPoint.y, WinGetLastError(hab));
else printf("    GpiSetPatternRefPoint %d,%d OK\n", refPoint.x, refPoint.y);
#endif
	oldPattern = GpiQueryPatternSet(hps);
#ifdef DEBUG
if (rc==LCID_ERROR) printf("    GpiQueryPatternSet ERROR %x\n", WinGetLastError(hab));
else printf("    GpiQueryPatternSet OK\n");
#endif
*/
	/* Local ID 254 to keep from clashing with fonts as long as possible */
/*
	rc = GpiSetBitmapId(hps, todPtr->bitmap.handle, 254L);
#ifdef DEBUG
if (rc!=TRUE) printf("    GpiSetBitmapId %x ERROR %x\n", todPtr->bitmap.handle,
WinGetLastError(hab));
else printf("    GpiSetBitmapId %x OK\n", todPtr->bitmap.handle);
#endif
	rc = GpiSetPatternSet(hps, 254L);
#ifdef DEBUG
if (rc!=TRUE) printf("    GpiSetPatternSet ERROR %x\n", WinGetLastError(hab));
else printf("    GpiSetPatternSet OK\n", todPtr->bitmap.handle);
#endif
*/
TkOS2SetStipple(hps, todPtr->bitmap.hps, todPtr->bitmap.handle, refPoint.x,
refPoint.y, &oldPattern, &oldRefPoint);

	/*
	 * Create temporary drawing surface containing a copy of the
	 * destination equal in size to the bounding box of the object.
	 */
	
/*
	psMem = WinGetPS(HWND_DESKTOP);
#ifdef DEBUG
printf("    psMem %x\n", psMem);
#endif

	bmpInfo.cbFix = sizeof(BITMAPINFOHEADER2);
	bmpInfo.cx = width;
	bmpInfo.cy = height;
	bitmap = GpiCreateBitmap(psMem, &bmpInfo, 0L, NULL, NULL);
#ifdef DEBUG
if (rc==GPI_ERROR) printf("    GpiCreateBitmap %dx%d ERROR %x\n", width, height,
WinGetLastError(hab));
else printf("    GpiCreateBitmap %dx%d OK: %x\n", width, height, bitmap);
#endif
	oldBitmap = GpiSetBitmap(psMem, bitmap);
#ifdef DEBUG
if (rc==HBM_ERROR) printf("    GpiSetBitmap %x ERROR %x\n", bitmap,
WinGetLastError(hab));
else printf("    GpiSetBitmap %x OK: %x\n", bitmap, oldBitmap);
#endif
	GpiSetAttrs(psMem, PRIM_LINE, LBB_COLOR | LBB_WIDTH | LBB_TYPE, 0L,
	            &lineBundle);
*/
        /* Translate the Y coordinates to PM coordinates */
        aPoints[0].x = 0;	/* dest_x 0 */
        aPoints[0].y = windowHeight - height;	/* dest_y 0 */
        aPoints[1].x = width;	/* dest_x + width */
        aPoints[1].y = windowHeight;	/* dest_y + height */
        aPoints[2].x = rect.xLeft;
        aPoints[2].y = rect.yBottom;
/*
        GpiBitBlt(psMem, hps, 3, aPoints, ROP_SRCCOPY, BBO_IGNORE);
#ifdef DEBUG
if (rc!=TRUE) printf("    GpiBitBlt %x->%x ERROR %x\n", hps, psMem,
WinGetLastError(hab));
else printf("    GpiBitBlt %x->%x OK, aPoints (%d,%d)(%d,%d) (%d,%d)\n", hps,
psMem, aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y, aPoints[2].x,
aPoints[2].y);
#endif
*/

	/*
	 * Translate the object to 0,0 for rendering in the temporary drawing
	 * surface. 
	 */

	for (i = 0; i < npoints; i++) {
	    os2Points[i].x -= rect.xLeft;
	    /*
	    os2Points[i].y += (windowHeight - rect.yTop);
	    */
	    os2Points[i].y -= rect.yBottom;
	}

	/*
	 * Draw the object in the foreground color and copy it to the
	 * destination wherever the pattern is set.
	 */

/*
	GpiSetColor(psMem, gc->foreground);
	GpiSetPattern(psMem, PATSYM_SOLID);
*/
GpiSetColor(hps, gc->foreground);
GpiSetPattern(hps, PATSYM_SOLID);
GpiSetMix(hps, FM_AND);
GpiSetBackMix(hps, BM_LEAVEALONE);
	if (func == TOP_POLYGONS) {
int i;
/*
	    GpiMove(psMem, os2Points);
*/
GpiMove(hps, os2Points);
            polygon.ulPoints = npoints-1;
            polygon.aPointl = os2Points+1;
	    rc = GpiPolygons(psMem, 1, &polygon, POLYGON_BOUNDARY |
	                     (gc->fill_rule == EvenOddRule) ? POLYGON_ALTERNATE
	                                                    : POLYGON_WINDING,
	                     POLYGON_INCL);
#ifdef DEBUG
printf("GpiPolygons with");
for (i=0; i<npoints; i++) {
printf(" (%d,%d)", os2Points[i].x, os2Points[i].y);
}
printf(" returns %d\n", rc);
#endif
	} else { /* TOP_POLYLINE */
int i;
/*
	    GpiMove(psMem, os2Points);
*/
GpiMove(hps, os2Points);
            rc = GpiPolyLine(psMem, npoints-1, os2Points+1);
#ifdef DEBUG
printf("GpiPolyLine with");
for (i=0; i<npoints; i++) {
printf(" (%d,%d)", os2Points[i].x, os2Points[i].y);
}
printf(" returns %d\n", rc);
#endif
	}
        aPoints[0].x = rect.xLeft;	/* dest_x */
        aPoints[0].y = rect.yBottom;
        aPoints[1].x = rect.xRight;	/* dest_x + width */
        aPoints[1].y = rect.yTop;	/* dest_y */
        aPoints[2].x = 0;	/* src_x 0 */
        aPoints[2].y = windowHeight;	/* Src_y */
/*
        GpiBitBlt(hps, psMem, 3, aPoints, COPYFG, BBO_IGNORE);
*/

	/*
	 * If we are rendering an opaque stipple, then draw the polygon in the
	 * background color and copy it to the destination wherever the pattern
	 * is clear.
	 */

	if (gc->fill_style == FillOpaqueStippled) {
/*
	    GpiSetColor(psMem, gc->background);
*/
GpiSetColor(hps, gc->background);
GpiSetMix(hps, FM_SUBTRACT);
GpiSetBackMix(hps, BM_LEAVEALONE);
	    if (func == TOP_POLYGONS) {
                polygon.ulPoints = npoints;
                polygon.aPointl = os2Points;
/*
	        GpiPolygons(psMem, 1, &polygon,
*/
GpiPolygons(hps, 1, &polygon,
	                   (gc->fill_rule == EvenOddRule) ? POLYGON_ALTERNATE
	                                                  : POLYGON_WINDING,
	                   0);
	    } else { /* TOP_POLYLINE */
/*
                GpiPolyLine(psMem, npoints, os2Points);
*/
GpiPolyLine(hps, npoints, os2Points);
	    }
/*
            GpiBitBlt(hps, psMem, 3, aPoints, COPYBG, BBO_IGNORE);
*/
	}
	/* end of using 254 */
/*
	GpiDeleteSetId(hps, 254L);
        GpiSetPatternSet(hps, oldPattern);
	GpiSetBitmap(psMem, oldBitmap);
	GpiDeleteBitmap(oldBitmap);
*/
TkOS2UnsetStipple(hps, todPtr->bitmap.hps, todPtr->bitmap.handle, oldPattern,
&oldRefPoint);
	WinReleasePS(psMem);
    } else {

	GpiQueryAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_WIDTH | LBB_TYPE,
	              &oldLineBundle);
	GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_WIDTH | LBB_TYPE, 0L,
	            &lineBundle);
        oldPattern = GpiQueryPattern(hps);
        oldColor = GpiQueryColor(hps);
	GpiSetColor(hps, gc->foreground);
	GpiSetPattern(hps, PATSYM_SOLID);
	GpiSetMix(hps, mixModes[gc->function]);

        if (func == TOP_POLYGONS) {
int i;
	    GpiMove(hps, os2Points);
            polygon.ulPoints = npoints-1;
            polygon.aPointl = os2Points+1;
            rc = GpiPolygons(hps, 1, &polygon, POLYGON_BOUNDARY |
                             (gc->fill_rule == EvenOddRule) ? POLYGON_ALTERNATE
                                                            : POLYGON_WINDING,
	                     POLYGON_INCL);
#ifdef DEBUG
printf("RenderObject: GpiPolygons with");
for (i=0; i<npoints; i++) {
printf(" (%d,%d)", os2Points[i].x, os2Points[i].y);
}
printf("returns %d\n", rc);
#endif
        } else { /* TOP_POLYLINE */
int i;
	    GpiMove(hps, os2Points);
            rc = GpiPolyLine(hps, npoints-1, os2Points+1);
#ifdef DEBUG
printf("RenderObject: GpiPolyLine with");
for (i=0; i<npoints; i++) {
printf(" (%d,%d)", os2Points[i].x, os2Points[i].y);
}
printf("returns %d\n", rc);
#endif
        }

	GpiSetColor(hps, oldColor);
	GpiSetPattern(hps, oldPattern);
	GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_WIDTH | LBB_TYPE, 0L,
	            &oldLineBundle);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawLines --
 *
 *	Draw connected lines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders a series of connected lines.
 *
 *----------------------------------------------------------------------
 */

void
XDrawLines(display, d, gc, points, npoints, mode)
    Display* display;
    Drawable d;
    GC gc;
    XPoint* points;
    int npoints;
    int mode;
{
    LINEBUNDLE lineBundle;
    TkOS2PSState state;
    HPS hps;

#ifdef DEBUG
printf("XDrawLines\n");
#endif
    
    if (d == None) {
	return;
    }

    hps = TkOS2GetDrawablePS(display, d, &state);

    lineBundle.lColor = gc->foreground;
    lineBundle.fxWidth = gc->line_width;
    lineBundle.usType = LINETYPE_SOLID;
    RenderObject(hps, gc, d, points, npoints, mode, &lineBundle, TOP_POLYLINE);
    
    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * XFillPolygon --
 *
 *	Draws a filled polygon.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a filled polygon on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XFillPolygon(display, d, gc, points, npoints, shape, mode)
    Display* display;
    Drawable d;
    GC gc;
    XPoint* points;
    int npoints;
    int shape;
    int mode;
{
    LINEBUNDLE lineBundle;
    TkOS2PSState state;
    HPS hps;

#ifdef DEBUG
printf("XFillPolygon\n");
#endif

    if (d == None) {
	return;
    }

    hps = TkOS2GetDrawablePS(display, d, &state);

    lineBundle.usType = LINETYPE_INVISIBLE;
    RenderObject(hps, gc, d, points, npoints, mode, &lineBundle, TOP_POLYGONS);

    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawRectangle --
 *
 *	Draws a rectangle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a rectangle on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XDrawRectangle(display, d, gc, x, y, width, height)
    Display* display;
    Drawable d;
    GC gc;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
{
    LINEBUNDLE lineBundle, oldLineBundle;
    TkOS2PSState state;
    LONG oldPattern;
    HPS hps;
    POINTL oldCurrent, changePoint;
    LONG windowHeight;

#ifdef DEBUG
printf("XDrawRectangle\n");
#endif

    if (d == None) {
	return;
    }

    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)d);

    hps = TkOS2GetDrawablePS(display, d, &state);

    GpiQueryAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_WIDTH | LBB_TYPE,
                  &oldLineBundle);
    lineBundle.lColor = gc->foreground;
    lineBundle.fxWidth = gc->line_width;
    lineBundle.usType = LINETYPE_SOLID;
    GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_WIDTH | LBB_TYPE, 0L,
                &lineBundle);
    oldPattern = GpiQueryPattern(hps);
    GpiSetPattern(hps, PATSYM_NOSHADE);
    GpiSetMix(hps, mixModes[gc->function]);

    GpiQueryCurrentPosition(hps, &oldCurrent);
    changePoint.x = x;
    /* Translate the Y coordinates to PM coordinates */
    changePoint.y = windowHeight - y;
    GpiSetCurrentPosition(hps, &changePoint);
    /* Now put other point of box in changePoint */
    changePoint.x += width+1;
    changePoint.y -= (height-1);	/* PM coordinates are reverse */
    GpiBox(hps, DRO_OUTLINE, &changePoint, 0L, 0L);
    GpiSetCurrentPosition(hps, &oldCurrent);

    GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_WIDTH | LBB_TYPE, 0L,
                &oldLineBundle);
    GpiSetPattern(hps, oldPattern);
    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawArc --
 *
 *	Draw an arc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws an arc on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XDrawArc(display, d, gc, x, y, width, height, angle1, angle2)
    Display* display;
    Drawable d;
    GC gc;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    int angle1;
    int angle2;
{
#ifdef DEBUG
printf("XDrawArc\n");
#endif
    display->request++;

    DrawOrFillArc(display, d, gc, x, y, width, height, angle1, angle2, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * XFillArc --
 *
 *	Draw a filled arc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a filled arc on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XFillArc(display, d, gc, x, y, width, height, angle1, angle2)
    Display* display;
    Drawable d;
    GC gc;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    int angle1;
    int angle2;
{
#ifdef DEBUG
printf("XFillArc\n");
#endif
    display->request++;

    DrawOrFillArc(display, d, gc, x, y, width, height, angle1, angle2, 1);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawOrFillArc --
 *
 *	This procedure handles the rendering of drawn or filled
 *	arcs and chords.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the requested arc.
 *
 *----------------------------------------------------------------------
 */

static void
DrawOrFillArc(display, d, gc, x, y, width, height, angle1, angle2, fill)
    Display *display;
    Drawable d;
    GC gc;
    int x, y;			/* left top */
    unsigned int width, height;
    int angle1;			/* angle1: three-o'clock (deg*64) */
    int angle2;			/* angle2: relative (deg*64) */
    int fill;			/* ==0 draw, !=0 fill */
{
    HPS hps;
    LONG brush, oldPattern;
    LINEBUNDLE lineBundle, oldLineBundle;
    int sign;
    POINTL center, curPt;
    TkOS2PSState state;
    int xr, yr, xstart, ystart, xend, yend;
    double radian_start, radian_end, radian_tmp;
    LONG windowHeight;
    ARCPARAMS arcParams, oldArcParams;
    double a1sin, a1cos;

#ifdef DEBUG
printf("DrawOrFillArc %d,%d, w %d, h %d, a1 %d, a2 %d, %s\n", x, y, width,
height, angle1, angle2, fill ? "fill" : "nofill");
#endif

    if (d == None) {
	return;
    }
    a1sin = sin(XAngleToRadians(angle1));
    a1cos = cos(XAngleToRadians(angle1));
    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)d);

    /* Translate the Y coordinates to PM coordinates */
    y = windowHeight - y;
    /* Translate angles back to positive degrees */
    angle1 = abs(angle1 / 64);
    if (angle2 < 0) {
        sign = -1;
    }
    else {
        sign = 1;
    }
    angle2 = abs(angle2 / 64);

    hps = TkOS2GetDrawablePS(display, d, &state);

    GpiSetMix(hps, mixModes[gc->function]);

    /*
     * Now draw a filled or open figure.
     */

    rc = GpiQueryArcParams(hps, &oldArcParams);
    arcParams.lP = width / 2;
    arcParams.lQ = sign * (height / 2);
    arcParams.lR = 0;
    arcParams.lS = 0;
    rc = GpiSetArcParams(hps, &arcParams);
    GpiQueryAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_WIDTH | LBB_TYPE,
                  &oldLineBundle);
    /* Center of arc is at x+(0.5*width),y-(0.5*height) */
    center.x = x + (0.5 * width);
    center.y = y - (0.5 * height);	/* PM y coordinate reversed */
    if (!fill) {
        lineBundle.lColor = gc->foreground;
        lineBundle.fxWidth = gc->line_width;
        lineBundle.usType = LINETYPE_SOLID;
        GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_WIDTH | LBB_TYPE, 0L,
                    &lineBundle);
        oldPattern = GpiQueryPattern(hps);
        GpiSetPattern(hps, PATSYM_NOSHADE);
	/* direction of arc is determined by arc parameters, while angles are
	 * always positive
	 * p*q > r*s -> direction counterclockwise
	 * p*q < r*s -> direction clockwise
	 * p*q = r*s -> straight line
	 * When comparing the Remarks for function GpiSetArcParams in the GPI
	 * Guide and Reference with the Xlib Programming Manual (Fig.6-1),
	 * the 3 o'clock point of the unit arc is defined by (p,s) and the 12
	 * o'clock point by (r,q), when measuring from (0,0) -> (cx+p, cy+s) and
	 * (cx+r, cy+q) from center of arc at (cx, cy).
	 * => p = 0.5 width, q = (sign*)0.5 height, r=s=0
	 * GpiPartialArc draws a line from the current point to the start of the
	 * partial arc, so we have to set the current point to it first.
	 * this is (cx+0.5*width*cos(angle1), cy+sign*0.5*height*sin(angle1))
	 */
	curPt.x = center.x + (int) (0.5 * width * a1cos);
	curPt.y = center.y + (int) (0.5 * sign * height * a1sin);
	rc = GpiSetCurrentPosition(hps, &curPt);
	rc= GpiPartialArc(hps, &center, MAKEFIXED(1, 0), MAKEFIXED(angle1, 0),
                          MAKEFIXED(angle2, 0));
        GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_WIDTH | LBB_TYPE, 0L,
                    &oldLineBundle);
        GpiSetPattern(hps, oldPattern);
    } else {
        oldPattern = GpiQueryPattern(hps);
        GpiSetPattern(hps, PATSYM_SOLID);
        GpiSetColor(hps, gc->foreground);
        lineBundle.lColor = gc->foreground;
        lineBundle.fxWidth = gc->line_width;
        lineBundle.usType = LINETYPE_SOLID;
        GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_WIDTH | LBB_TYPE, 0L,
                    &lineBundle);
	if (gc->arc_mode == ArcChord) {
            /* Chord */
            /*
             * See GPI reference: first do GpiPartialArc with invisible line,
             * then again with visible line, in an Area for filling.
             */
	    GpiSetLineType(hps, LINETYPE_INVISIBLE);
	    GpiPartialArc(hps, &center, MAKEFIXED(1, 0), MAKEFIXED(angle1, 0),
                          MAKEFIXED(angle2, 0));
	    GpiSetLineType(hps, LINETYPE_SOLID);
	    GpiBeginArea(hps, BA_BOUNDARY|BA_ALTERNATE);
	    GpiPartialArc(hps, &center, MAKEFIXED(1, 0), MAKEFIXED(angle1, 0),
                          MAKEFIXED(angle2, 0));
	    GpiEndArea(hps);
	} else if ( gc->arc_mode == ArcPieSlice ) {
            /* Pie */
	    GpiSetCurrentPosition(hps, &center);
	    GpiBeginArea(hps, BA_BOUNDARY|BA_ALTERNATE);
            rc = GpiPartialArc(hps, &center, MAKEFIXED(1, 0), MAKEFIXED(angle1, 0),
                               MAKEFIXED(angle2, 0));
            GpiLine(hps, &center);
	    GpiEndArea(hps);
	}
        GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_WIDTH | LBB_TYPE, 0L,
                    &oldLineBundle);
        GpiSetPattern(hps, oldPattern);
    }
    rc = GpiSetArcParams(hps, &oldArcParams);
    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * TkScrollWindow --
 *
 *	Scroll a rectangle of the specified window and accumulate
 *	a damage region.
 *
 * Results:
 *	Returns 0 if the scroll genereated no additional damage.
 *	Otherwise, sets the region that needs to be repainted after
 *	scrolling and returns 1.
 *
 * Side effects:
 *	Scrolls the bits in the window.
 *
 *----------------------------------------------------------------------
 */

int
TkScrollWindow(tkwin, gc, x, y, width, height, dx, dy, damageRgn)
    Tk_Window tkwin;		/* The window to be scrolled. */
    GC gc;			/* GC for window to be scrolled. */
    int x, y, width, height;	/* Position rectangle to be scrolled. */
    int dx, dy;			/* Distance rectangle should be moved. */
    TkRegion damageRgn;		/* Region to accumulate damage in. */
{
    HWND hwnd = TkOS2GetHWND(Tk_WindowId(tkwin));
    RECTL scrollRect;
    LONG lReturn;
    LONG windowHeight;

#ifdef DEBUG
printf("TkScrollWindow\n");
#endif

    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)Tk_WindowId(tkwin));

    /* Translate the Y coordinates to PM coordinates */
    y = windowHeight - y;
    dy = -dy;
    scrollRect.xLeft = x;
    scrollRect.yTop = y;
    scrollRect.xRight = x + width;
    scrollRect.yBottom = y - height;	/* PM coordinate reversed */
    /* Hide cursor, just in case */
    WinShowCursor(hwnd, FALSE);
    lReturn = WinScrollWindow(hwnd, dx, dy, &scrollRect, NULL, (HRGN) damageRgn,
                              NULL, 0);
    /* Show cursor again */
    WinShowCursor(hwnd, TRUE);
    return ( lReturn == RGN_NULL ? 0 : 1);
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2SetStipple --
 *
 *	Set the pattern set of a HPS to a "stipple" (bitmap).
 *
 * Results:
 *	Returns the old pattern set and reference point.
 *
 * Side effects:
 *	Unsets the bitmap in/from "its" HPS, appoints a bitmap ID to it,
 *	sets that ID as the pattern set, with its reference point as given.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2SetStipple(destPS, bmpPS, stipple, x, y, oldPatternSet, oldRefPoint)
    HPS destPS;		/* The HPS to receive the stipple. */
    HPS bmpPS;		/* The HPS of the stipple-bitmap. */
    HBITMAP stipple;	/* Stipple-bitmap. */
    LONG x, y;			/* Reference point for the stipple. */
    LONG *oldPatternSet;	/* Pattern set that was in effect in the HPS. */
    PPOINTL oldRefPoint;	/* Reference point that was in effect. */
{
    POINTL refPoint;

#ifdef DEBUG
printf("TkOS2SetStipple destPS %x, bmpPS %x, stipple %x, (%d,%d)\n", destPS,
bmpPS, stipple, x, y);
#endif
    refPoint.x = x;
    refPoint.y = y;
    rc = GpiQueryPatternRefPoint(destPS, oldRefPoint);
#ifdef DEBUG
if (rc!=TRUE) printf("    GpiQueryPatternRefPoint ERROR %x\n", WinGetLastError(hab));
else printf("    GpiQueryPatternRefPoint OK: %d,%d\n", oldRefPoint->x, oldRefPoint->y);
#endif
    rc = GpiSetPatternRefPoint(destPS, &refPoint);
#ifdef DEBUG
if (rc!=TRUE) printf("    GpiSetPatternRefPoint ERROR %x\n", WinGetLastError(hab));
else printf("    GpiSetPatternRefPoint %d,%d OK\n", refPoint.x, refPoint.y);
#endif
    *oldPatternSet = GpiQueryPatternSet(destPS);
#ifdef DEBUG
if (rc==LCID_ERROR) printf("    GpiQueryPatternSet ERROR %x\n", WinGetLastError(hab));
else printf("    GpiQueryPatternSet %x\n", oldPatternSet);
#endif
    GpiSetBitmap(bmpPS, NULLHANDLE);
    rc = GpiSetBitmapId(destPS, stipple, 254L);
#ifdef DEBUG
if (rc!=TRUE) printf("    GpiSetBitmapId %x ERROR %x\n", stipple, WinGetLastError(hab));
else printf("    GpiSetBitmapId %x OK\n", stipple);
#endif
    rc = GpiSetPatternSet(destPS, 254L);
#ifdef DEBUG
if (rc!=TRUE) printf("    GpiSetPatternSet ERROR %x\n", WinGetLastError(hab));
else printf("    GpiSetPatternSet OK\n");
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2UnsetStipple --
 *
 *	Unset the "stipple" (bitmap) from a HPS.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Unsets the bitmap in/from "its" HPS, appoints a bitmap ID to it,
 *	sets that ID as the pattern set, with its reference point as given.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2UnsetStipple(destPS, bmpPS, stipple, oldPatternSet, oldRefPoint)
    HPS destPS;		/* The HPS to give up the stipple. */
    HPS bmpPS;		/* The HPS of the stipple-bitmap. */
    HBITMAP stipple;	/* Stipple-bitmap. */
    LONG oldPatternSet;		/* Pattern set to be put back in effect. */
    PPOINTL oldRefPoint;	/* Reference point to put back in effect. */
{
#ifdef DEBUG
printf("TkOS2UnsetStipple destPS %x, bmpPS %x, stipple %x, oldRP %d,%d\n",
destPS, bmpPS, stipple, oldRefPoint->x, oldRefPoint->y);
#endif
    rc = GpiSetPatternSet(destPS, oldPatternSet);
    rc = GpiSetPatternRefPoint(destPS, oldRefPoint);

    rc = GpiDeleteSetId(destPS, 254L);
    /* end of using 254 */
    /* The bitmap must be reselected in the HPS */
    GpiSetBitmap(bmpPS, stipple);
}
