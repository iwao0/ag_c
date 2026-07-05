// time.h minimal runtime calls
// Expected: exit=0
#include <assert.h>
#include <time.h>

int main(void) {
    time_t stored = -1;
    time_t now = time(&stored);
    assert(stored == now);
    assert(clock() >= 0);
    assert(difftime((time_t)10, (time_t)3) == 7.0);
    struct timespec ts = {0, 0};
    assert(timespec_get(&ts, TIME_UTC) == TIME_UTC);
#ifdef __wasm32__
    assert(ts.tv_sec == 0);
    assert(ts.tv_nsec == 0);
#else
    assert(ts.tv_nsec >= 0);
    assert(ts.tv_nsec < 1000000000L);
#endif
    assert(timespec_get(&ts, 0) == 0);
    return 0;
}
