#ifndef __util_h_
#define __util_h_

#include <stdlib.h>
#include <stdio.h>

#define CHECK_MALLOC(x) if(!x) { printf("Out of memory\n"); exit(EXIT_FAILURE); }

#endif // __util_h_

