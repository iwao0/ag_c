// #if 定数式の短絡評価: && / || / ?: の未選択側は評価されない (C11 6.10.1)。
// ゼロ除算を含む未選択側が評価されるとコンパイル失敗する。
// c-testsuite 00145 と同形。
#include <assert.h>

int main(void) {
    int ok = 0;

#if 0 != (0 && (0 / 0))
#error 0 != (0 && (0 / 0))
#else
    ok |= 1;
#endif

#if 1 != (-1 || (0 / 0))
#error 1 != (-1 || (0 / 0))
#else
    ok |= 2;
#endif

#if 3 != (-1 ? 3 : (0 / 0))
#error 3 != (-1 ? 3 : (0 / 0))
#else
    ok |= 4;
#endif

    assert(ok == 7);
    return 0;
}
