// typedef 経由の unsigned char/short 戻り型は、宣言幅へゼロ拡張して返す必要がある
// (C11 6.8.6.4 / 6.3.1.2)。resolve_func_ret_typedef が typedef の unsigned 性を
// 捨てていたため戻り値が符号拡張され、`u8 f(){return 200;}` が -56 に化けていた
// (uint8_t ローカルと同根の戻り型版)。signed char typedef は符号拡張のまま維持。
#include <assert.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef signed char s8;

u8 fu8(void) { return 200; }      // ゼロ拡張で 200
u16 fu16(void) { return 50000; }  // ゼロ拡張で 50000
s8 fs8(void) { return -5; }       // 符号拡張で -5

int main(void) {
    int a = fu8();
    assert(a == 200);

    int b = fu16();
    assert(b == 50000);

    int c = fs8();
    assert(c == -5);
    return 0;
}
