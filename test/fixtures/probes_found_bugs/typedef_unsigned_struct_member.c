// typedef 経由の unsigned char/short/int を struct メンバ型に使うと、メンバの
// unsigned 性が失われ sub-int ロードが符号拡張 (ldrsb/ldrsh/ldrsw) されていた。
// struct_layout.c の typedef メンバ分岐が typedef の is_unsigned を取得しながら
// member_is_unsigned に反映していなかった (u8 等は IDENT トークンで TK_UNSIGNED
// 検出に掛からない)。さらに cast wrapper が operand 自体を signed 型へ塗り替えると、
// `(int)s.a` / `(signed)s.a` の load 符号性も壊れる。signed char typedef メンバは
// 符号拡張のまま維持。
#include <assert.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;

struct S {
    u8 a;
    u16 b;
    u32 c;
    s8 d;
};

int main(void) {
    struct S s;
    s.a = 200;
    s.b = 50000;
    s.c = 0xFFFFFFFFU;
    s.d = -5;

    int va = (int)s.a;
    assert(va == 200);          // ゼロ拡張 (符号拡張なら -56)
    int va_signed = (signed)s.a;
    assert(va_signed == 200);   // cast 結果は signed int だが operand load はゼロ拡張

    int vb = (int)s.b;
    assert(vb == 50000);        // ゼロ拡張 (符号拡張なら負)
    int vb_signed = (signed)s.b;
    assert(vb_signed == 50000); // cast 結果は signed int だが operand load はゼロ拡張

    unsigned long vc = (unsigned long)s.c;
    assert(vc == 4294967295UL); // 32->64 ゼロ拡張

    int vd = (int)s.d;
    assert(vd == -5);           // signed char は符号拡張維持
    return 0;
}
