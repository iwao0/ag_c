// stdlib.h strtol/strtoul integer conversion
// Expected: exit=0
#include <assert.h>
#include <stdlib.h>

int main(void) {
    char *end = 0;
    long a = strtol("  -2aZ", &end, 16);
    assert(a == -42);
    assert(*end == 'Z');

    char binary[] = "101xyz";
    unsigned long b = strtoul(binary, &end, 2);
    assert(b == 5);
    assert(end == binary + 3);

    assert(strtol("77", &end, 0) == 77);
    assert(*end == 0);

    assert(strtoul("+0x1f!", &end, 16) == 31);
    assert(*end == '!');

    char no_digits[] = "   +xyz";
    end = 0;
    assert(strtol(no_digits, &end, 10) == 0);
    assert(end == no_digits);

    char no_unsigned[] = "  -";
    end = 0;
    assert(strtoul(no_unsigned, &end, 10) == 0);
    assert(end == no_unsigned);
    return 0;
}
