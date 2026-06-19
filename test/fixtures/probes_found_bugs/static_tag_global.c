// ファイルスコープで storage class (static/extern 等) が tag 型 (struct/union/enum)
// の前に付く宣言 (`static struct S g = {...}`) を受理する必要がある。
// parse_toplevel_decl_spec が tag 判定前に修飾子を読み飛ばさず、`static` を見た
// 時点で tag キーワードを認識できず E3016 で弾いていた。static int 等の builtin は
// psx_consume_type_kind が内部で skip するため元から通っていた。
#include <assert.h>

struct S { int a; int b; };
union U { int n; char c[4]; };
enum E { A = 5, B = 10 };

static struct S gs = {3, 4};
static union U gu = {0x41};
static enum E ge = B;

int main(void) {
    assert(gs.a == 3);
    assert(gs.b == 4);
    assert(gu.n == 0x41);
    assert(gu.c[0] == 0x41);   // little-endian の最下位バイト
    assert(ge == 10);
    return 0;
}
