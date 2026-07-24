// stdlib.h integer function pointer and aggregate return ABI boundaries
// Expected: exit=0
#include <stdlib.h>

int main(void) {
    int (*abs_value)(int) = abs;
    long (*labs_value)(long) = labs;
    long long (*llabs_value)(long long) = llabs;
    div_t (*divide_int)(int, int) = div;
    ldiv_t (*divide_long)(long, long) = ldiv;
    lldiv_t (*divide_long_long)(long long, long long) = lldiv;
    div_t int_result;
    ldiv_t long_result;
    lldiv_t long_long_result;

    if (abs_value(-42) != 42) return 1;
    if (labs_value(-123456789L) != 123456789L) return 2;
    if (llabs_value(-1234567890123LL) != 1234567890123LL) return 3;

    int_result = divide_int(-17, 5);
    if (int_result.quot != -3 || int_result.rem != -2) return 4;

    long_result = divide_long(123456789L, 1000L);
    if (long_result.quot != 123456L || long_result.rem != 789L) return 5;

    long_long_result = divide_long_long(-9223372036854775807LL, 10LL);
    if (long_long_result.quot != -922337203685477580LL ||
        long_long_result.rem != -7LL) {
        return 6;
    }
    return 0;
}
