// `b = a = E` の値は a の型へ変換した値 (C11 6.5.16p3)。sub-int (char/short)
// lvalue では rhs をそのまま返していたため、外側 b が変換前の値のままになっていた
// (IMM は coerce で切り詰められず、値拡張も常に SEXT)。格納後に lvalue を再ロード
// して正しい幅・符号の値を返す (lvar/gvar/deref/member 全経路。アドレスに副作用が
// あるときは二重評価を避ける)。
#include <assert.h>

unsigned char g;

int main(void) {
    // local unsigned char: 300 -> 44
    unsigned char a; int b;
    b = a = 300;
    assert(a == 44);
    assert(b == 44);

    // signed char: 200 -> -56 (符号拡張)
    signed char sa; int sb;
    sb = sa = 200;
    assert(sa == -56);
    assert(sb == -56);

    // global unsigned char
    int gb;
    gb = g = 300;
    assert(g == 44);
    assert(gb == 44);

    // deref (unsigned char *)
    unsigned char buf[1]; unsigned char *p = buf; int pb;
    pb = *p = 300;
    assert(buf[0] == 44);
    assert(pb == 44);

    // struct member
    struct S { unsigned char x; } s; int mb;
    mb = s.x = 300;
    assert(s.x == 44);
    assert(mb == 44);

    // 副作用アドレス *q++ = v が二重評価されないこと
    unsigned char arr[3] = {0, 0, 0};
    unsigned char *q = arr;
    *q++ = 5;
    *q++ = 6;
    assert(q == arr + 2);
    assert(arr[0] == 5);
    assert(arr[1] == 6);
    return 0;
}
