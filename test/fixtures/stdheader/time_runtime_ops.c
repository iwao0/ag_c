// time.h minimal runtime calls
// Expected: exit=0
#include <assert.h>
#include <time.h>

int main(void) {
    time_t stored = 7;
    time_t now = time(&stored);
    assert(now == (time_t)-1);
    assert(stored == (time_t)-1);
    assert(clock() == (clock_t)-1);
    assert(difftime((time_t)10, (time_t)3) == 7.0);
    struct timespec ts = {7, 9};
    assert(timespec_get(&ts, TIME_UTC) == 0);
    assert(ts.tv_sec == 7);
    assert(ts.tv_nsec == 9);
    assert(timespec_get(&ts, 0) == 0);
    return 0;
}
