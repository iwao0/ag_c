// unsigned 整数と浮動小数点の相互変換が signed 変換で lower されるバグ。
// 修正前:
//   - (double)4294967295U が -1.0 相当として扱われる
//   - 4294967295.0 -> unsigned int が signed trunc になり、backend によって trap/誤値
#include <assert.h>

static double pass_double(double x) {
    return x;
}

static double return_unsigned_as_double(void) {
    unsigned int x = 4294967295U;
    return x;
}

static int take_unsigned(unsigned int x) {
    return x == 4294967295U;
}

static unsigned int return_double_as_unsigned(void) {
    return 4294967295.0;
}

int main(void) {
    unsigned int u = 4294967295U;
    double d = u;
    assert(d > 4294967294.0);

    double max_u32 = 4294967295.0;
    unsigned int roundtrip = max_u32;
    assert(roundtrip == 4294967295U);

    double casted = (double)4294967295U;
    assert(casted > 4294967294.0);

    assert(pass_double(u) > 4294967294.0);
    assert(return_unsigned_as_double() > 4294967294.0);
    assert(take_unsigned(4294967295.0));
    assert(return_double_as_unsigned() == 4294967295U);
    return 0;
}
