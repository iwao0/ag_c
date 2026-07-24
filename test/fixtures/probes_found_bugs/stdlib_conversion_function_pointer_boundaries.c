// stdlib.h conversion function pointer, end pointer, and range boundaries
// Expected: exit=0
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

int main(void) {
    int (*to_int)(const char *) = atoi;
    long (*to_long)(const char *) = atol;
    long long (*to_long_long)(const char *) = atoll;
    double (*to_double)(const char *) = atof;
    long (*parse_long)(const char *, char **, int) = strtol;
    unsigned long (*parse_unsigned_long)(const char *, char **, int) = strtoul;
    long long (*parse_long_long)(const char *, char **, int) = strtoll;
    unsigned long long (*parse_unsigned_long_long)(const char *, char **, int) = strtoull;
    float (*parse_float)(const char *, char **) = strtof;
    double (*parse_double)(const char *, char **) = strtod;
    long double (*parse_long_double)(const char *, char **) = strtold;
    const char long_overflow[] = "9223372036854775808!";
    const char long_underflow[] = "-9223372036854775809!";
    const char unsigned_overflow[] = "18446744073709551616!";
    const char no_digits[] = "xyz";
    char *end;

    if (to_int(" -42x") != -42) return 1;
    if (to_long("123456789") != 123456789L) return 2;
    if (to_long_long("-1234567890123") != -1234567890123LL) return 3;
    if (to_double("1.25") != 1.25) return 4;

    if (parse_long("  -0x2a!", &end, 0) != -42L || *end != '!') return 5;
    if (parse_unsigned_long("177?", &end, 8) != 127UL || *end != '?') return 6;
    if (parse_long_long("-1234567890123x", &end, 10) != -1234567890123LL ||
        *end != 'x') {
        return 7;
    }
    if (parse_unsigned_long_long("ffffffffffffffff.", &end, 16) != ULLONG_MAX ||
        *end != '.') {
        return 8;
    }

    if (parse_float("0.5x", &end) != 0.5f || *end != 'x') return 9;
    if (parse_double("-6.25!", &end) != -6.25 || *end != '!') return 10;
    if (parse_long_double("1.25e2?", &end) != 125.0L || *end != '?') return 11;

    errno = 0;
    if (parse_long(long_overflow, &end, 10) != LONG_MAX ||
        errno != ERANGE || end != long_overflow + 19) {
        return 12;
    }
    errno = 0;
    if (parse_long(long_underflow, &end, 10) != LONG_MIN ||
        errno != ERANGE || end != long_underflow + 20) {
        return 13;
    }
    errno = 0;
    if (parse_unsigned_long(unsigned_overflow, &end, 10) != ULONG_MAX ||
        errno != ERANGE || end != unsigned_overflow + 20) {
        return 14;
    }
    errno = 0;
    if (parse_long_long(long_underflow, &end, 10) != LLONG_MIN ||
        errno != ERANGE || end != long_underflow + 20) {
        return 15;
    }
    errno = 0;
    if (parse_unsigned_long_long(unsigned_overflow, &end, 10) != ULLONG_MAX ||
        errno != ERANGE || end != unsigned_overflow + 20) {
        return 16;
    }

    end = (char *)0;
    if (parse_long(no_digits, &end, 10) != 0 || end != no_digits) return 17;
    return 0;
}
