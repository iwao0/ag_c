// Wasm standalone stdlib.h/inttypes.h div-family stubs
// Expected: exit=0
#include <inttypes.h>
#include <stdlib.h>

int main(void) {
    div_t a = div(17, 5);
    if (a.quot != 3 || a.rem != 2) return 1;

    div_t b = div(-17, 5);
    if (b.quot != -3 || b.rem != -2) return 2;

    ldiv_t c = ldiv(123456789L, 1000L);
    if (c.quot != 123456L || c.rem != 789L) return 3;

    lldiv_t d = lldiv(-123456789LL, 1000LL);
    if (d.quot != -123456LL || d.rem != -789LL) return 4;

    imaxdiv_t e = imaxdiv(9223372036854775807LL, 10LL);
    if (e.quot != 922337203685477580LL || e.rem != 7LL) return 5;

    if (imaxabs((intmax_t)-42) != 42) return 6;
    return 0;
}
