// time.h mktime runtime call
// Expected: exit=0
#include <time.h>

static int expect_tm(struct tm *tm, int sec, int min, int hour, int mday,
                     int mon, int year, int wday, int yday) {
    if (tm->tm_sec != sec) return 1;
    if (tm->tm_min != min) return 2;
    if (tm->tm_hour != hour) return 3;
    if (tm->tm_mday != mday) return 4;
    if (tm->tm_mon != mon) return 5;
    if (tm->tm_year != year) return 6;
    if (tm->tm_wday != wday) return 7;
    if (tm->tm_yday != yday) return 8;
    if (tm->tm_isdst != 0) return 9;
    return 0;
}

int main(void) {
    struct tm carry = {0};
    struct tm leap = {0};
    struct tm month = {0};
    time_t t;

    carry.tm_year = 69;
    carry.tm_mon = 11;
    carry.tm_mday = 31;
    carry.tm_hour = 23;
    carry.tm_min = 59;
    carry.tm_sec = 70;
    t = mktime(&carry);
#ifdef __wasm32__
    if (t != 10) return 1;
#endif
    if (expect_tm(&carry, 10, 0, 0, 1, 0, 70, 4, 0) != 0) return 2;

    leap.tm_year = 72;
    leap.tm_mon = 1;
    leap.tm_mday = 29;
    leap.tm_hour = 12;
    t = mktime(&leap);
#ifdef __wasm32__
    if (t != 68212800) return 3;
#endif
    if (expect_tm(&leap, 0, 0, 12, 29, 1, 72, 2, 59) != 0) return 4;

    month.tm_year = 70;
    month.tm_mon = 13;
    month.tm_mday = 32;
    t = mktime(&month);
#ifdef __wasm32__
    if (t != 36892800) return 5;
#endif
    if (expect_tm(&month, 0, 0, 0, 4, 2, 71, 4, 62) != 0) return 6;

    return 0;
}
