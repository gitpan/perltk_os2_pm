/* 
 * tkOS2Mem.c --
 *
 *	Functions for memory management.
 *
 * Copyright (c) 1997 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkOS2Int.h"


/*
 *----------------------------------------------------------------------
 *
 * TkOS2AllocMem --
 *
 *	Allocate memory, read/write access and committed.
 *
 * Results:
 *	Base address of allocated memory or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
TkOS2AllocMem (size)
size_t	size;
{
    void *mem;
    APIRET rc;
#ifdef DEBUG
    printf("TkOS2AllocMem %d\n", size);
#endif
    rc = DosAllocMem(&mem, size, PAG_READ|PAG_WRITE|PAG_COMMIT);
    if (rc == NO_ERROR) {
#ifdef DEBUG
    printf("    returning %x\n", mem);
#endif
        return (void *)mem;
    }
    else {
#ifdef DEBUG
    printf("    DosAllocMem ERROR %d\n", rc);
#endif
        return (void *)NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2FreeMem --
 *
 *	Free memory.
 *
 * Results:
 *	Memory is freed, unless NULL was given.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2FreeMem (mem)
void	*mem;
{
    if (mem != (void *)NULL) DosFreeMem((PVOID)mem);
}
