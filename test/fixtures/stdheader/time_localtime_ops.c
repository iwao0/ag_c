// time.h localtime runtime call
// Expected: exit=0
#include <time.h>

int main(void) {
    time_t t = 0;
    struct tm *tm = localtime(&t);
    if (!tm) return 1;
    if (tm->tm_sec < 0 || tm->tm_sec > 60) return 2;
    if (tm->tm_min < 0 || tm->tm_min > 59) return 3;
    if (tm->tm_hour < 0 || tm->tm_hour > 23) return 4;
    return 0;
}
