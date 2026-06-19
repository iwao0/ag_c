// NaN < 0.0 は偽 (順序不定)
// 期待: exit=1
#include <assert.h>
int main(void) {
    double x = 0.0 / 0.0;
    _Bool lt = (x < 0.0);   // NaN との順序比較は偽
    assert(lt == 0);
    return 0;
}
