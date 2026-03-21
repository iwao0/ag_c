#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC 1000000

time_t time(time_t *tloc);
clock_t clock(void);
double difftime(time_t end, time_t beginning);

#endif
