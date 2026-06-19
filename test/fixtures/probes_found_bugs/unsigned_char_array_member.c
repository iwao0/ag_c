// struct の unsigned char/short 配列メンバの要素ロードはゼロ拡張する必要がある。
// build_member_deref_node が配列メンバに pointee_is_unsigned を立てていなかったため
// `s.x[0]=200` が ldrsb で -56 に化けた (スカラメンバは元から OK)。さらにサイズ1配列
// メンバは struct_layout で array_len=0 のスカラに潰れるため、build_subscript_deref で
// base 自身の is_unsigned も最終要素へ伝播する。typedef alias も含む。
#include <assert.h>

typedef unsigned char u8;

struct A { unsigned char x[2]; };   // 通常の配列メンバ
struct B { unsigned char x[1]; };   // サイズ1 (struct_layout でスカラに潰れる)
struct C { unsigned short x[2]; };  // unsigned short 配列
struct D { u8 x[2]; };              // typedef alias 配列
struct E { signed char x[2]; };     // signed は符号拡張維持

int main(void) {
    struct A a; a.x[0] = 200; a.x[1] = 250;
    assert((int)a.x[0] == 200);
    assert((int)a.x[1] == 250);

    struct B b; b.x[0] = 200;
    assert((int)b.x[0] == 200);

    struct C c; c.x[0] = 50000; c.x[1] = 60000;
    assert((int)c.x[0] == 50000);
    assert((int)c.x[1] == 60000);

    struct D d; d.x[0] = 200; d.x[1] = 199;
    assert((int)d.x[0] == 200);
    assert((int)d.x[1] == 199);

    struct E e; e.x[0] = -5; e.x[1] = -100;
    assert((int)e.x[0] == -5);
    assert((int)e.x[1] == -100);
    return 0;
}
