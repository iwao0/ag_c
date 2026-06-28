// unsigned 整数と浮動小数点の相互変換が signed 変換で lower されるバグ。
// 修正前:
//   - (double)4294967295U が -1.0 相当として扱われる
//   - 4294967295.0 -> unsigned int が signed trunc になり、backend によって trap/誤値
#include <assert.h>

int main(void) {
    unsigned int u = 4294967295U;
    double d = u;
    assert(d > 4294967294.0);

    double max_u32 = 4294967295.0;
    unsigned int roundtrip = max_u32;
    assert(roundtrip == 4294967295U);

    double casted = (double)4294967295U;
    assert(casted > 4294967294.0);
    return 0;
}
