#pragma once

#include <exec/types.h>

/* An AmigaOS interrupt that calls tick about rate_mhz times per 1000 seconds:
   a CIA-B timer when one is free, otherwise the vertical blank.
   ticker_claim picks the source and returns the rate it really runs at,
   ticker_run starts it. */
ULONG ticker_claim(ULONG rate_mhz, void (*tick)(void));
void ticker_run(void);
void ticker_stop(void);
const char *ticker_source(void);
