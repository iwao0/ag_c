// c-testsuite 00200 縮約: シフト式の結果型は右辺ではなく integer promotion 後の左辺型。
// 併せて、cast 結果型の sizeof が long / unsigned long を保持することを固定する。
#include <assert.h>

#define PTYPE(M) ((M) < 0 || -(M) < 0 ? -1 : 1) * (int)sizeof((M) + 0)

int main(void) {
    assert(sizeof((long)1) == 8);
    assert(sizeof((unsigned long)1) == 8);
    assert(sizeof((long long)1) == 8);
    assert(sizeof((unsigned long long)1) == 8);

    assert(PTYPE((short)1 << (long)1) == PTYPE((short)1));
    assert(PTYPE((unsigned short)1 << (long long)1) == PTYPE((unsigned short)1));
    assert(PTYPE((unsigned)1 << (long)1) == PTYPE((unsigned)1));
    assert(PTYPE((long)1 << (int)1) == PTYPE((long)1));
    assert(PTYPE((unsigned long long)1 << (short)1) == PTYPE((unsigned long long)1));
    return 0;
}
