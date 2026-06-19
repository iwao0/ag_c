// 2 件のビットフィールド系バグ:
//  B2: enum 型ビットフィールドが符号付き扱いされ、最上位ビットを使う値が負に化けて
//      いた。非負列挙の enum は unsigned 扱い (clang と同じ) に修正。
//  B3: グローバル/静的 struct のビットフィールド初期化子が、同一 storage ユニットの
//      先頭フィールドしか出力せず残りが 0 になっていた。同一ユニットのビットフィールド
//      を 1 整数に詰めて出力するよう修正。
#include <assert.h>

enum E { A, B, C, D };
struct EB { enum E e : 2; };

struct Pack { unsigned a : 4; unsigned b : 4; };
struct Pack3 { unsigned a : 2; unsigned b : 2; unsigned c : 2; };
struct Mixed { unsigned a : 4; int x; unsigned b : 4; };

/* B3: 静的初期化 */
struct Pack g_pack = { 3, 5 };           /* 0x53 = 1 バイト */
struct Pack3 g_pack3 = { 1, 2, 3 };
struct Mixed g_mixed = { 3, 7, 5 };

int main(void) {
    /* B2: enum ビットフィールドは unsigned */
    struct EB s;
    s.e = 3;
    assert(s.e == 3);

    /* 通常のビットフィールド符号は維持 */
    struct { int x : 4; } sn;
    sn.x = -3;
    assert(sn.x == -3);
    struct { unsigned y : 3; } su;
    su.y = 7;
    assert(su.y == 7);

    /* B3: 静的ビットフィールド初期化が全フィールド反映 */
    assert(g_pack.a == 3);
    assert(g_pack.b == 5);
    assert(g_pack3.a == 1);
    assert(g_pack3.b == 2);
    assert(g_pack3.c == 3);
    assert(g_mixed.a == 3);
    assert(g_mixed.x == 7);
    assert(g_mixed.b == 5);
    return 0;
}
