/* 
 * tkOS2Image.c --
 *
 *	This file contains routines for manipulation full-color images.
 *
 * Copyright (c) 1996-1997 Illya Vaes
 * Copyright (c) 1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkOS2Int.h"

static int		PutPixel (XImage *image, int x, int y,
			    unsigned long pixel);

/*
 *----------------------------------------------------------------------
 *
 * PutPixel --
 *
 *	Set a single pixel in an image.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
PutPixel(image, x, y, pixel)
    XImage *image;
    int x, y;
    unsigned long pixel;
{
    LONG rgb;
    char *destPtr;
    /* Translate Y coordinate to PM */
/*
    y = image->height - y;
*/

    destPtr = &(image->data[(y * image->bytes_per_line)
    	+ (x * (image->bits_per_pixel >> 3))]);

#ifdef DEBUG
    printf("PutPixel %x", pixel);
#endif
rc = GpiQueryLogColorTable(globalPS, 0, pixel, 1L, &rgb);
pixel = rgb;
#ifdef DEBUG
                if (rc == QLCT_ERROR) {
                    printf("    GpiQueryLogColorTable ERROR %x\n",
                           WinGetLastError(hab));
                } else if (rc == QLCT_RGB) {
                    printf("    table in RGB mode, no element returned");
                } else {
                    printf("    color %x", rgb);
                }
#endif
/*
    destPtr[0] = GetBValue(pixel);
    destPtr[1] = GetGValue(pixel);
    destPtr[2] = GetRValue(pixel);
    destPtr[3] = 0;
#ifdef DEBUG
    printf(" (B%x,G%x,R%x)\n", destPtr[0], destPtr[1], destPtr[2]);
#endif
*/
    destPtr[0] = 0;
    destPtr[1] = GetRValue(pixel);
    destPtr[2] = GetGValue(pixel);
    destPtr[3] = GetBValue(pixel);
#ifdef DEBUG
    printf(" (R%x,G%x,B%x)\n", destPtr[0], destPtr[1], destPtr[2]);
#endif
    return 0;
}

static void
imfree(XImage *ximage)
{
    if (ximage->data) ckfree(ximage->data);
    ckfree(ximage);
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateImage --
 *
 *	Allocates storage for a new XImage.
 *
 * Results:
 *	Returns a newly allocated XImage.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XImage *
XCreateImage(display, visual, depth, format, offset, data, width, height,
	bitmap_pad, bytes_per_line)
    Display* display;
    Visual* visual;
    unsigned int depth;
    int format;
    int offset;
    char* data;
    unsigned int width;
    unsigned int height;
    int bitmap_pad;
    int bytes_per_line;
{
    XImage* imagePtr = (XImage *) ckalloc(sizeof(XImage));

#ifdef DEBUG
printf("XCreateImage\n");
#endif
    if (imagePtr) {
        imagePtr->width = width;
        imagePtr->height = height;
        imagePtr->xoffset = offset;
        imagePtr->format = format;
        imagePtr->data = data;
        imagePtr->byte_order = MSBFirst;
        imagePtr->bitmap_unit = 32;
        imagePtr->bitmap_bit_order = MSBFirst;
        imagePtr->bitmap_pad = bitmap_pad;
        imagePtr->depth = depth;

        /*
         * Round to the nearest word boundary.
         */
    
        imagePtr->bytes_per_line = bytes_per_line ? bytes_per_line
 	    : ((depth * width + 31) >> 3) & ~3;
    
        /*
         * If the screen supports TrueColor, then we use 3 bytes per
         * pixel, and we have to install our own pixel routine.
         */
    
        if (visual->class == TrueColor) {
	    imagePtr->bits_per_pixel = 24;
	    imagePtr->f.put_pixel = PutPixel;
        } else {
	    imagePtr->bits_per_pixel = 8;
	    imagePtr->f.put_pixel = NULL;
        }
        imagePtr->red_mask = visual->red_mask;
        imagePtr->green_mask = visual->green_mask;
        imagePtr->blue_mask = visual->blue_mask;
        imagePtr->f.create_image = NULL;
        imagePtr->f.destroy_image = &imfree;
        imagePtr->f.get_pixel = NULL;
        imagePtr->f.sub_image = NULL;
        imagePtr->f.add_pixel = NULL;
    }
    
    return imagePtr;
}
