// `int (*t)[3]; *(t+N)[K]` は `*((t+N)[K])` と解釈される。(t+N)[K] は配列 int[3] へ
// decay してアドレスになり、外側 `*` がその先頭要素をロードする。
// ポインタ算術 `t+N` の ND_ADD ノードが pointer-to-array の多段ストライドを失い、
// make_subscript_scaled_offset が inner_deref_size を拾えず結果 deref_size=0 →
// 配列へ decay せずスカラ load → 外側 `*` が値をアドレスとして deref し SIGBUS だった。
// ND_ADD/ND_SUB の base からポインタ被演算子の inner ストライドを引き継いで修正。
#include <assert.h>

int main(void) {
    int arr[3][3] = {{10, 20, 30}, {40, 50, 60}, {70, 80, 90}};
    int (*t)[3] = arr;

    // *(t+N)[0] == *((t+N)[0]) == *(*(t+N)) == arr[N][0]
    assert(*(t + 0)[0] == 10);
    assert(*(t + 1)[0] == 40);
    assert(*(t + 2)[0] == 70);

    // 等価な書き方は元から動作 (退行確認)
    assert((*(t + 1))[0] == 40);
    assert((*(t + 1))[2] == 60);
    assert(t[1][0] == 40);
    assert(t[2][2] == 90);

    // スカラポインタ算術は無影響 (退行確認)
    int a[4] = {1, 2, 3, 4};
    int *p = a;
    assert(*(p + 2) == 3);
    assert(p[1] == 2);
    return 0;
}
