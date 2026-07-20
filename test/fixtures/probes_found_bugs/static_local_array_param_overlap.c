// 続き60: static local 配列を含む関数のパラメータがスタック上で重なるバグ (修正)。
//
// 修正前: `void f(int *out) { static int data[5]={..}; int i; ... }` のような関数で、
// `find_owning_lvar` が offset=0 を検索すると static-local-lowering の alias lvar
// (offset=0, size=0) が storage リストの先頭にあるため先に拾われ、本来の仮引数
// `out` (size=8) の代わりに alias が「所有者」として 4 バイト alloca を発行していた。
// 結果、`out` のスロットが 4 バイト幅で確保され、後続ローカル `i` がフレーム上で
// out と重なり、`i = 2` が `out` の上位 32bit を破壊して `out[i] = data[i]` が
// 不正アドレスに store して SIGSEGV。
//
// 修正: ir_builder.c::find_owning_lvar で is_static_local の alias を skip する。
// alias は実体がグローバルにあるためスタック上には存在せず、所有者ではない。
#include <assert.h>

void getarr(int *out) {
    static int data[5] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++) out[i] = data[i];
}

/* 関数間で static local 配列が独立に動作することも確認 */
int sum_static(void) {
    static int data[3] = {1, 2, 3};
    int total = 0;
    for (int i = 0; i < 3; i++) total += data[i];
    return total;
}

/* static local 配列のあとに複数 local を置く: 重なりがないこと */
int complex_layout(int *out) {
    static int table[4] = {100, 200, 300, 400};
    int i = 0;
    int sum = 0;
    int max = 0;
    for (i = 0; i < 4; i++) {
        sum += table[i];
        if (table[i] > max) max = table[i];
        out[i] = table[i];
    }
    /* `sum`, `max`, `i` のいずれも `out` と重ならないこと */
    return sum + max;
}

int main(void) {
    int buf[5];
    getarr(buf);
    assert(buf[0] == 10);
    assert(buf[1] == 20);
    assert(buf[2] == 30);
    assert(buf[3] == 40);
    assert(buf[4] == 50);

    assert(sum_static() == 6);
    /* 2 回目の呼び出しでも static local がリセットされない (初期化は 1 回のみ) */
    assert(sum_static() == 6);

    int buf2[4];
    int r = complex_layout(buf2);
    assert(r == 1400);  /* 100+200+300+400 + 400 max */
    assert(buf2[0] == 100);
    assert(buf2[3] == 400);

    return 0;
}
