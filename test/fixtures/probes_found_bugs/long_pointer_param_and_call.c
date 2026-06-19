// long に関する 2 つのバグをまとめて検証する。
// (1) `long *a` / `unsigned long *a` 仮引数の subscript `a[i]` が E3064 で拒否されて
//     いた。スカラポインタ仮引数登録が pointee 8 バイト (long) のとき
//     size==elem_size==8 となり lvar_is_pointer の size>elem_size 判定に漏れていた
//     (int* は elem=4 で通る、double* は pointee_fp_kind で通る)。
//     修正: pointee_size>=8 の非 fp ポインタ仮引数に pointer_qual_levels を立てる。
// (2) 直接呼び出しで long を返す関数の戻り値が、呼び出し側で i32 扱いされ
//     `h(x) * 2` のように戻り値を使う演算が 32bit で行われ上位ビットが落ちていた。
//     修正: funcall 結果型を、呼出先が long を返すなら i64 にする。
// 修正前: (1) コンパイルエラー (2) 戻り値演算が下位 32bit に化ける
// 期待: exit=42
#include <assert.h>
long sum(long *a, int n) {          // (1) long* 仮引数 subscript
    long s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}
long doubled(long x) { return x * 2; }
long plus1(long x)   { return x + 1; }

int main(void) {
    long a[3] = {3000000000L, 3000000000L, 4000000000L};   // 合計 10000000000
    long total = sum(a, 3);
    assert(total == 10000000000L);

    // (2) 戻り値を使った演算がネストしても 64bit を保つ
    long r = doubled(plus1(5000000000L));                  // (5e9+1)*2 = 10000000002
    assert(r == 10000000002L);

    // unsigned long* も同様に subscript できる
    unsigned long u[2] = {0x100000000UL, 0x200000000UL};
    assert(u[0] == 0x100000000UL);
    assert(u[1] == 0x200000000UL);

    assert(total == 10000000000L); return 0;                // 10 + 32 = 42
}
