// inttypes.h signatures and aggregate return ABI through indirect calls.
// Expected: exit=0
#include <inttypes.h>
#include <stddef.h>

static intmax_t (*imaxabs_signature)(intmax_t) = imaxabs;
static imaxdiv_t (*imaxdiv_signature)(intmax_t, intmax_t) = imaxdiv;
static intmax_t (*strtoimax_signature)(const char *, char **, int) = strtoimax;
static uintmax_t (*strtoumax_signature)(const char *, char **, int) = strtoumax;

int main(void) {
    const char signed_text[] = " -2aZ";
    const char unsigned_text[] = "100000001x";
    const char no_value[] = "  +z";
    char *end = 0;
    imaxdiv_t result;

    if (imaxabs_signature((intmax_t)-4294967297LL) !=
        (intmax_t)4294967297LL) return 1;

    result = imaxdiv_signature(
        (intmax_t)9223372036854775807LL, (intmax_t)10);
    if (result.quot != (intmax_t)922337203685477580LL ||
        result.rem != (intmax_t)7) return 2;

    result = imaxdiv_signature((intmax_t)-4294967297LL, (intmax_t)1000);
    if (result.quot != (intmax_t)-4294967LL ||
        result.rem != (intmax_t)-297LL) return 3;

    if (strtoimax_signature(signed_text, &end, 16) != (intmax_t)-42 ||
        end != signed_text + 4) return 4;
    if (strtoumax_signature(unsigned_text, &end, 2) != (uintmax_t)257 ||
        end != unsigned_text + 9) return 5;
    if (strtoimax_signature(no_value, &end, 10) != 0 || end != no_value)
        return 6;
    return 0;
}
