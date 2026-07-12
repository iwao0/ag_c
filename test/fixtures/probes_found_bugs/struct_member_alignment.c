// 続き65: ネストした struct メンバのアラインメントが sizeof と混同される bug (修正)。
//
// 修正前: 非ポインタの struct メンバの member_align を elem_size (= sizeof) で決めていた。
// `struct Inner {int a, b;}` は sizeof=8 / align=4 だが elem_size=8 が採用され、
// `struct Outer { struct Inner i; int trail; }` の agg_align が 8 に膨張。結果
// sizeof(Outer) が 12 ではなく 16 (4 バイト過剰パディング)。
//
// 修正: struct/union メンバ (非ポインタ) は ps_ctx_get_tag_align で取得した
// アラインメントを使う (= メンバ内最大スカラのアラインメント)。
#include <assert.h>

struct Inner { int a, b; };                       /* sizeof=8, align=4 */
struct Outer { struct Inner i; int trail; };      /* sizeof=12, align=4 */

/* 多段ネスト: align 4 が伝播 */
struct InnerN { int x[3]; };                       /* sizeof=12, align=4 */
struct MidN { struct InnerN n; int t; };           /* sizeof=16, align=4 */
struct OuterN { struct MidN m; int u; };           /* sizeof=20, align=4 */

/* double を含む struct は align=8 のままなのを確認 (回帰確認) */
struct WithD { int a; double d; };                 /* sizeof=16, align=8 */
struct ContainsD { struct WithD wd; int t; };      /* sizeof=24, align=8 */

int main(void) {
    assert(sizeof(struct Inner) == 8);
    assert(sizeof(struct Outer) == 12);  /* 修正前は 16 だった */

    assert(sizeof(struct InnerN) == 12);
    assert(sizeof(struct MidN) == 16);
    assert(sizeof(struct OuterN) == 20);

    /* double-containing は影響なし */
    assert(sizeof(struct WithD) == 16);
    assert(sizeof(struct ContainsD) == 24);

    /* メンバアクセスが正しいオフセットで読めるか */
    struct Outer o;
    o.i.a = 10;
    o.i.b = 20;
    o.trail = 30;
    assert(o.i.a == 10);
    assert(o.i.b == 20);
    assert(o.trail == 30);

    return 0;
}
