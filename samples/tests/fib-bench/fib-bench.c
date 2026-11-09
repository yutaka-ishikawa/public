//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "app/eapp_utils.h"

unsigned long read_ticks(void)
{
    unsigned long ticks;
    asm volatile("rdtime %0" : "=r"(ticks));
    return ticks;
}

unsigned long fibonacci_rec(unsigned long in)
{
    if (in <= 1) {
	return 1;
    } else {
	return fibonacci_rec(in - 1) + fibonacci_rec(in - 2);
    }
}

// Returns the number of ticks for a given fibonacci execution
unsigned long fib_eapp(unsigned long in)
{
    unsigned long start;
    unsigned long end;
    volatile unsigned long result;

    start = read_ticks();
    result = fibonacci_rec(in);
    (void)result;
    end = read_ticks();
    return end - start;
}

int EAPP_ENTRY main(void)
{
    int arg = 35;
    EAPP_RETURN(fib_eapp(arg));
}
