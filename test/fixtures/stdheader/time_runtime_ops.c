// time.h minimal runtime calls
// Expected: exit=0
#include <assert.h>
#include <time.h>

int main(void) {
    time_t (*read_time)(time_t *) = time;
    clock_t (*read_clock)(void) = clock;
    int (*read_timespec)(struct timespec *, int) = timespec_get;
    assert(read_time != 0);
    assert(read_clock != 0);
    assert(read_timespec != 0);
    assert(difftime((time_t)10, (time_t)3) == 7.0);
    struct timespec ts = {7, 9};
    /* Invalid bases are deterministic and do not query the host clock. */
    assert(timespec_get(&ts, 0) == 0);
    return 0;
}
