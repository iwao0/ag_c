// time.h text conversion runtime calls
// Expected: exit=0
#include <string.h>
#include <time.h>

int main(void) {
    time_t t = 0;
    struct tm *tm = gmtime(&t);
    if (!tm) return 1;
    if (strcmp(asctime(tm), "Thu Jan  1 00:00:00 1970\n") != 0) return 2;
#ifdef __wasm32__
    if (strcmp(ctime(&t), "Thu Jan  1 00:00:00 1970\n") != 0) return 3;
    t = 86400;
    tm = gmtime(&t);
    if (!tm) return 4;
    if (strcmp(asctime(tm), "Fri Jan  2 00:00:00 1970\n") != 0) return 5;
    if (strcmp(ctime(&t), "Fri Jan  2 00:00:00 1970\n") != 0) return 6;
#else
    if (!ctime(&t)) return 3;
#endif
    return 0;
}
