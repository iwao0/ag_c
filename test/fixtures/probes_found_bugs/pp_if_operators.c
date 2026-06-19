// `#if` 定数式 (C11 6.10.1) はビット演算 (& | ^)、シフト (<< >>)、剰余 (%)、
// 三項 (?:) を全てサポートする必要がある。プリプロセッサの定数式パーサに
// これらのレベルが無く `#if (3 & 5) == 1` 等が E1006 でコンパイル失敗していた。
// 優先順位: conditional > logor > logand > bitor > bitxor > bitand > equality >
// relational > shift > add > mul(% 追加) を追加。
#include <assert.h>

int main(void) {
    int ok = 0;

#if (3 & 5) == 1
    ok |= 1;
#endif
#if (3 | 4) == 7
    ok |= 2;
#endif
#if (6 ^ 3) == 5
    ok |= 4;
#endif
#if (1 << 4) == 16
    ok |= 8;
#endif
#if (255 >> 4) == 15
    ok |= 16;
#endif
#if (17 % 5) == 2
    ok |= 32;
#endif
#if (1 ? 7 : 9) == 7 && (0 ? 7 : 9) == 9
    ok |= 64;
#endif
    // 優先順位: & は == より低い、<< は + より低い
#if (1 << 2 + 1) == 8 && (2 + 1 & 3) == 3
    ok |= 128;
#endif

    assert(ok == 255);
    return 0;
}
