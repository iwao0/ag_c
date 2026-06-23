// #line 指令の行番号引数はマクロ展開後に解釈される (C11 6.10.1)。
// `#line line` で line=1000 が展開されないと __LINE__ が物理行のまま残る。
// c-testsuite 00152 と同形。
#include <assert.h>

#undef line
#define line 1000

#line line
#if 1000 != __LINE__
#error "#line line" not work as expected
#endif

int main(void) {
    return 0;
}
