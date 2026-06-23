// 続き64: `(char*)&struct_var` のキャストでポインタ性が消える bug (修正)。
//
// 修正前: `wrap_as_addr` が struct/union の `&s` に対して is_tag_pointer=1 のみ立て、
// is_pointer=0 のまま返していた。`apply_cast` の `(char*)operand` 経路 (line 2119) は
// `ps_node_is_pointer(operand)` のみで条件分岐しており、is_tag_pointer 側を見ない
// ため `(char*)&s` が ND_PTR_CAST にラップされず元の ND_ADDR が返ってきた。
//
// 影響: `(char*)&s.c - (char*)&s` のような offsetof 風のポインタ減算で、
// ND_SUB の lhs/rhs 両辺が「ポインタ - ポインタ = ptrdiff_t」と認識されず、
// `long o = (char*)&s.c - (char*)&s;` が「scalar 変数をポインタで初期化」E3064 で拒否されていた。
// 同形を assign で書くと通り、init 形だけ落ちるという奇妙な挙動。
//
// 修正: apply_cast でオペランドが is_tag_pointer のときも ND_PTR_CAST ラッパを通す。
#include <assert.h>

struct S { char c; int i; double d; };
struct T { int v[4]; };

int main(void) {
    /* (a) offsetof 風 — メンバアドレスからベースアドレスを引いて offset を取る */
    struct S s;
    long off_c = (char*)&s.c - (char*)&s;
    long off_i = (char*)&s.i - (char*)&s;
    long off_d = (char*)&s.d - (char*)&s;
    assert(off_c == 0);
    assert(off_i > 0);
    assert(off_d > off_i);

    /* (b) union/struct どちらでも (char*)&u - (char*)&u は 0 */
    struct T t;
    assert((char*)&t - (char*)&t == 0);

    /* (c) struct 配列でも element 間距離 */
    struct S arr[3];
    long dist = (char*)&arr[2] - (char*)&arr[0];
    assert(dist == 2 * (long)sizeof(struct S));

    /* (d) assign 形 (修正前から動作、回帰確認) */
    long o;
    o = (char*)&s.c - (char*)&s;
    assert(o == 0);

    return 0;
}
