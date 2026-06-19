// >8 バイトの struct compound literal を代入文の右辺に置くと ir_build_module
// failed でコンパイル失敗していた (`s = (struct S){9,8,7}`)。初期化子形は通り、
// 同じ struct の struct-to-struct 代入も通っていた。build_assign_struct が src に
// ND_COMMA(init, temp) 形の compound literal を扱わず fail していたのを修正。
#include <assert.h>

struct S3 { int a, b, c; };
struct S4 { int a, b, c, d; };
struct Outer { struct S3 s; };

int main(void) {
    struct S3 s;
    s = (struct S3){ 9, 8, 7 };          // 12 バイト
    assert(s.a == 9 && s.b == 8 && s.c == 7);

    struct S4 q;
    q = (struct S4){ 1, 2, 3, 4 };        // 16 バイト
    assert(q.a == 1 && q.b == 2 && q.c == 3 && q.d == 4);

    // メンバを代入対象に
    struct Outer t;
    t.s = (struct S3){ 5, 6, 7 };
    assert(t.s.a == 5 && t.s.b == 6 && t.s.c == 7);

    // 上書き再代入
    s = (struct S3){ 100, 0, 20 };
    assert(s.a == 100 && s.b == 0 && s.c == 20);
    return 0;
}
