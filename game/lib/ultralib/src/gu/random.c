/**************************************************************************
 *									  *
 *		 Copyright (C) 1995, Silicon Graphics, Inc.		  *
 *									  *
 *  These coded instructions, statements, and computer programs  contain  *
 *  unpublished  proprietary  information of Silicon Graphics, Inc., and  *
 *  are protected by Federal copyright law.  They  may  not be disclosed  *
 *  to  third  parties  or copied or duplicated in any form, in whole or  *
 *  in part, without the prior written consent of Silicon Graphics, Inc.  *
 *									  *
 **************************************************************************/

#include "guint.h"
#include "StadionPort.h"

/*
 *  Return a pseudorandom 32 bit number
 *  try the RAND macro too
 *   
 */
static unsigned int xseed = 174823885; // PORT: file scope, for Port_GuRandomSeed

// PORT: battle cross-check (src/port/xcheck.cpp): both games restart the generator at each move animation, so the
// particles that draw from it match
void Port_GuRandomSeed(unsigned int seed)
{
    xseed = seed;
}

extern int gPortXcheckTraceGuRandom;              // PORT: battle cross-check (src/port/xcheck.cpp)

int guRandom(void)
{
    unsigned int x;

    if (gPortXcheckTraceGuRandom) {
        Port_XcheckGuRandomCalled(__builtin_return_address(0));
    }

    x = (xseed<<2) + 2;

    x *= (x+1);
    x = x >> 2;

    xseed = x;

    return( x );
}
