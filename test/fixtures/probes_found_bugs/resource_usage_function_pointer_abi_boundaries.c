// sys/resource.h function pointer, target layout, and failure boundaries.
// Expected: exit=0
#include <errno.h>
#include <stddef.h>
#include <sys/resource.h>

struct guarded_usage {
    unsigned long before;
    struct rusage usage;
    unsigned long after;
};

static int (*getrusage_signature)(int, struct rusage *) = getrusage;

int main(void) {
    struct guarded_usage guarded = {0};
    volatile int self_scope = RUSAGE_SELF;
    volatile int children_scope = RUSAGE_CHILDREN;

    if (self_scope != 0 || children_scope != -1) return 1;

    guarded.before = 0x1122334455667788UL;
    guarded.after = 0x8877665544332211UL;
    guarded.usage.ru_maxrss = 1234567;

#ifdef __wasm32__
    if (sizeof(struct rusage) != 8 ||
        offsetof(struct rusage, ru_maxrss) != 0) {
        return 2;
    }

    errno = 0;
    if (getrusage_signature(RUSAGE_SELF, &guarded.usage) != -1) return 3;
    if (errno != ENOSYS) return 4;
    if (guarded.before != 0x1122334455667788UL ||
        guarded.usage.ru_maxrss != 1234567 ||
        guarded.after != 0x8877665544332211UL) {
        return 5;
    }
#else
    if (sizeof(struct timeval) != 16 ||
        offsetof(struct timeval, tv_sec) != 0 ||
        offsetof(struct timeval, tv_usec) != 8 ||
        sizeof(struct rusage) != 144 ||
        offsetof(struct rusage, ru_utime) != 0 ||
        offsetof(struct rusage, ru_stime) != 16 ||
        offsetof(struct rusage, ru_maxrss) != 32 ||
        offsetof(struct rusage, ru_minflt) != 64 ||
        offsetof(struct rusage, ru_nivcsw) != 136) {
        return 6;
    }

    if (getrusage_signature(RUSAGE_SELF, &guarded.usage) != 0) return 7;
    if (guarded.before != 0x1122334455667788UL ||
        guarded.after != 0x8877665544332211UL) {
        return 8;
    }
    if (guarded.usage.ru_utime.tv_sec < 0 ||
        guarded.usage.ru_utime.tv_usec < 0 ||
        guarded.usage.ru_utime.tv_usec >= 1000000 ||
        guarded.usage.ru_stime.tv_sec < 0 ||
        guarded.usage.ru_stime.tv_usec < 0 ||
        guarded.usage.ru_stime.tv_usec >= 1000000 ||
        guarded.usage.ru_maxrss < 0) {
        return 9;
    }
#endif

    return 0;
}
