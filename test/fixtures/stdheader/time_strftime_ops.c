// time.h strftime runtime call
// Expected: exit=0
#include <string.h>
#include <time.h>

int main(void) {
    time_t t = 0;
    struct tm *tm = gmtime(&t);
    char buf[128];
    char small[8];
    size_t n;
    if (!tm) return 1;
    n = strftime(buf, sizeof(buf), "%F %T %Y %m %d %H %M %S %%", tm);
    if (n != 41) return 2;
    if (strcmp(buf, "1970-01-01 00:00:00 1970 01 01 00 00 00 %") != 0) return 3;
    if (strftime(small, sizeof(small), "%Y-%m-%d", tm) != 0) return 4;
#ifdef __wasm32__
    t = 86400;
    tm = gmtime(&t);
    if (!tm) return 5;
    n = strftime(buf, sizeof(buf), "%a %b %e %C %D %I %j %p %R %x %X %y %z %Z %n%t%u %w", tm);
    if (strcmp(buf, "Fri Jan  2 19 01/02/70 12 002 AM 00:00 1970-01-02 00:00:00 70 +0000 UTC \n\t5 5") != 0) return 6;
    if (n != strlen(buf)) return 7;
    n = strftime(buf, sizeof(buf), "%c | %r", tm);
    if (strcmp(buf, "Fri Jan 02 00:00:00 1970 | 12:00:00 AM") != 0) return 8;
    if (n != strlen(buf)) return 9;
    t = 4 * 86400;
    tm = gmtime(&t);
    if (!tm) return 10;
    n = strftime(buf, sizeof(buf), "%U %W", tm);
    if (strcmp(buf, "01 01") != 0) return 11;
    if (n != strlen(buf)) return 12;
    n = strftime(buf, sizeof(buf), "%A %B", tm);
    if (strcmp(buf, "Monday January") != 0) return 13;
    if (n != strlen(buf)) return 14;
    t = 1609459200;
    tm = gmtime(&t);
    if (!tm) return 15;
    n = strftime(buf, sizeof(buf), "%G %g %V", tm);
    if (strcmp(buf, "2020 20 53") != 0) return 16;
    if (n != strlen(buf)) return 17;
#endif
    return 0;
}
