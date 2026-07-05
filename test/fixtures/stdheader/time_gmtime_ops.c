// time.h gmtime runtime call
// Expected: exit=0
#include <time.h>

int main(void) {
    time_t t = 0;
    struct tm *tm = gmtime(&t);
    if (!tm) return 1;
    if (tm->tm_sec != 0) return 2;
    if (tm->tm_min != 0) return 3;
    if (tm->tm_hour != 0) return 4;
    if (tm->tm_mday != 1) return 5;
    if (tm->tm_mon != 0) return 6;
    if (tm->tm_year != 70) return 7;
    if (tm->tm_wday != 4) return 8;
    if (tm->tm_yday != 0) return 9;
    if (tm->tm_isdst != 0) return 10;
    t = 86400;
    tm = gmtime(&t);
    if (!tm) return 11;
    if (tm->tm_sec != 0) return 12;
    if (tm->tm_min != 0) return 13;
    if (tm->tm_hour != 0) return 14;
    if (tm->tm_mday != 2) return 15;
    if (tm->tm_mon != 0) return 16;
    if (tm->tm_year != 70) return 17;
    if (tm->tm_wday != 5) return 18;
    if (tm->tm_yday != 1) return 19;
    if (tm->tm_isdst != 0) return 20;
    return 0;
}
