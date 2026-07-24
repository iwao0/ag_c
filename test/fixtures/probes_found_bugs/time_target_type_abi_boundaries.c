// time.h target type identity, signedness, and function-pointer boundaries.
// Expected: exit=0
#include <time.h>

struct guarded_time {
    unsigned long before;
    time_t value;
    unsigned long after;
};

static time_t (*time_signature)(time_t *) = time;
static clock_t (*clock_signature)(void) = clock;

int main(void) {
    struct guarded_time guarded = {0};
    volatile int time_kind = _Generic(
        (time_t)0,
        long: 1,
        default: 0);
    volatile int clock_kind = _Generic(
        (clock_t)0,
        long: 1,
        unsigned long: 2,
        default: 0);
    clock_t ticks;
    time_t result;

    if (time_kind != 1 || sizeof(time_t) != 8 || _Alignof(time_t) != 8)
        return 1;
#ifdef __wasm32__
    if (clock_kind != 1 || sizeof(clock_t) != 8 ||
        _Alignof(clock_t) != 8 || (clock_t)-1 >= 0)
        return 2;
#else
    if (clock_kind != 2 || sizeof(clock_t) != 8 ||
        _Alignof(clock_t) != 8 || (clock_t)-1 <= 0)
        return 3;
#endif

    guarded.before = 0x1122334455667788UL;
    guarded.value = 0;
    guarded.after = 0x8877665544332211UL;
    result = time_signature(&guarded.value);
    ticks = clock_signature();

    if (result != guarded.value)
        return 4;
#ifdef __wasm32__
    if (result != (time_t)-1 || ticks != (clock_t)-1)
        return 5;
#else
    (void)ticks;
#endif
    if (guarded.before != 0x1122334455667788UL ||
        guarded.after != 0x8877665544332211UL)
        return 6;
    return 0;
}
