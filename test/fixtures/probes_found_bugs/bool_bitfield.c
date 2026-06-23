// 続き56: `_Bool b : 1;` bitfield の符号性 (miscompile 修正)。
//
// 修正前: `_Bool b:1 = 1` を読み出すと bit_is_signed=1 のまま符号拡張され、
// 1bit signed extension で -1 に化け `(int)s.b == -1` になっていた。
// struct_layout で is_signed_type が TK_BOOL を見ても signed のまま放置されていたバグ。
//
// 修正: 型スペック loop 末尾で member_is_bool || member_is_unsigned なら
// is_signed_type=0 に上書きする。bit_is_signed=0 で 1bit zero extension に切り替わる。
//
// C11 6.7.2.1 / 6.3.1.2: _Bool は 0/1 のみを取る符号なし型。
#include <assert.h>

struct S1 {
    unsigned a : 3;
    _Bool   b : 1;
    unsigned c : 4;
};

struct S2 {
    _Bool b1 : 1;
    _Bool b2 : 1;
    _Bool b3 : 1;
};

/* unsigned 単独で書いた場合も同様に bitfield 符号性を確認 (回帰確認、修正前から OK) */
struct S3 {
    unsigned x : 5;
    unsigned y : 3;
};

int main(void) {
    /* (a) _Bool bitfield 1 を 1 として読み出せる */
    struct S1 s = { 7, 1, 15 };
    assert((int)s.b == 1);
    assert(s.a == 7);
    assert(s.c == 15);

    /* (b) 連続 _Bool bitfield */
    struct S2 t = { 1, 0, 1 };
    assert(t.b1 == 1);
    assert(t.b2 == 0);
    assert(t.b3 == 1);

    /* (c) 代入で更新できる */
    s.b = 0;
    assert((int)s.b == 0);
    s.b = 1;
    assert((int)s.b == 1);

    /* (d) unsigned 単独 bitfield (回帰確認) */
    struct S3 u = { 31, 7 };
    assert(u.x == 31);
    assert(u.y == 7);

    return 0;
}
