/* 
 * tkOS2Dll.c --
 *
 *	This file contains a stub dll entry point.
 *
 * Copyright (c) 1996-1997 Illya Vaes
 * Copyright (c) 1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkPort.h"
#include "tkOS2Int.h"

int _CRT_init(void);
void _CRT_term(void);

/* Save the Tk DLL handle for TkPerl */
unsigned long dllHandle = (unsigned long) NULLHANDLE;


/*
 *----------------------------------------------------------------------
 *
 * _DLL_InitTerm --
 *
 *	DLL entry point.
 *
 * Results:
 *	TRUE on sucess, FALSE on failure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned long _DLL_InitTerm(unsigned long modhandle, unsigned long flag)
{
    /*
     * If we are attaching to the DLL from a new process, tell Tk about
     * the hInstance to use. If we are detaching then clean up any
     * data structures related to this DLL.
     */

    switch (flag) {
    case 0:     /* INIT */
        _CRT_init();
        /* Save handle */
        dllHandle = modhandle;
        TkOS2InitPM();
        TkOS2XInit(TkOS2GetAppInstance());
        return TRUE;

    case 1:     /* TERM */
        TkOS2ExitPM();
        /* Invalidate handle */
        dllHandle = (unsigned long)NULLHANDLE;
        _CRT_term();
        break;
    }

    return TRUE;
}
