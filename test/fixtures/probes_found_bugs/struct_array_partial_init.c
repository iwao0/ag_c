// struct/union 要素の配列の部分初期化は、書かれなかったメンバ/要素を 0 にする
// (C11 6.7.9p21)。要素単位の 0-fill や scalar 0-fill ではメンバ単位の補完ができず
// `struct R a[2]={0}` の a[0].val 等が garbage、ネスト struct 配列は ir_build 失敗、
// `[i].field=` 連鎖 designator は E2006 だった。配列全体を先に 0 埋めしてから明示
// 初期化子で上書きし、`[i].member` designator もパースできるようにした。
#include <assert.h>

struct rec { int id; int val; };
struct inner { int a; int b; };
struct outer { struct inner in; int n; };

int main(void) {
    // no-brace 部分初期化: スカラが要素途中で尽きる
    struct rec a[2] = {0};
    assert(a[0].id == 0); assert(a[0].val == 0);
    assert(a[1].id == 0); assert(a[1].val == 0);

    struct rec b[2] = {5};
    assert(b[0].id == 5); assert(b[0].val == 0);
    assert(b[1].id == 0); assert(b[1].val == 0);

    // designated 要素 + 未指定要素の 0 補完
    struct rec c[3] = {[1] = {5, 6}};
    assert(c[0].id == 0); assert(c[0].val == 0);
    assert(c[1].id == 5); assert(c[1].val == 6);
    assert(c[2].id == 0); assert(c[2].val == 0);

    // [i].member = val 連鎖 designator (残メンバも 0)
    struct rec d[2] = {[1].id = 5};
    assert(d[0].id == 0); assert(d[0].val == 0);
    assert(d[1].id == 5); assert(d[1].val == 0);

    struct rec e[3] = {[0].id = 1, [0].val = 2, [2].id = 9};
    assert(e[0].id == 1); assert(e[0].val == 2);
    assert(e[1].id == 0); assert(e[1].val == 0);
    assert(e[2].id == 9); assert(e[2].val == 0);

    // ネスト struct を含む配列: brace と designator 両方
    struct outer f[2] = {[1] = {{1, 2}, 3}};
    assert(f[0].in.a == 0); assert(f[0].in.b == 0); assert(f[0].n == 0);
    assert(f[1].in.a == 1); assert(f[1].in.b == 2); assert(f[1].n == 3);

    struct outer g[2] = {[1].in.b = 7, [1].n = 3};
    assert(g[0].in.a == 0); assert(g[0].in.b == 0); assert(g[0].n == 0);
    assert(g[1].in.a == 0); assert(g[1].in.b == 7); assert(g[1].n == 3);

    // 完全初期化の退行確認
    struct rec h[2] = {{1, 2}, {3, 4}};
    assert(h[0].id == 1); assert(h[0].val == 2);
    assert(h[1].id == 3); assert(h[1].val == 4);
    return 0;
}
