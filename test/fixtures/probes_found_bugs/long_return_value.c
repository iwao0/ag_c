// long / long long を返す関数の戻り値が 32bit に切り詰められていたバグ。
// ir_type_from_node は long を i32 としか見ず、関数の ret_type が i32 になる。
// return 時に coerce_to_type が i64 値 (long 算術の結果など) を i32 へ TRUNC して
// 上位 32bit を捨てていた。`return x` (単純な変数) は値が PTR 型で coerce が
// 切り詰めず偶然動いていたが、`return a+b` のような i64 算術結果は切り詰められた。
// 修正: 戻り値型トークンを問い合わせ、scalar size>=8 (long) なら ret_type を i64。
// 修正前: long 戻り値の算術結果が下位 32bit に化ける
// 期待: exit=42
#include <assert.h>
long add(long a, long b) { return a + b; }      // long 算術を返す
long mul(long a, int n)  { return a * n; }
long big(void)           { return 10000000000L; } // 範囲外リテラルを返す

int main(void) {
    long s = add(5000000000L, 5000000000L);     // 10000000000
    assert(s == 10000000000L);

    long m = mul(3000000000L, 3);               // 9000000000
    assert(m == 9000000000L);

    long b = big();                             // 10000000000
    assert(b == 10000000000L);

    // 戻り値を直接演算に使う
    long total = add(1000000000L, 2000000000L) + mul(2000000000L, 2);  // 3e9 + 4e9 = 7e9
    assert(total == 7000000000L);

    // ここまで到達 = すべての long 戻り値が正しい。
    assert(total == 7000000000L); return 0;     // 7 + 35 = 42
}
