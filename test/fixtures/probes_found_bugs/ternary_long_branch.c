// 64bit 整数 (long / long long) を分岐に持つ条件演算子が、結果を 32bit に
// 切り詰めていたバグ。複数の原因が絡んでいた:
//   (1) build_node_ternary が結果型を常に i32/ポインタとしか見ず、long 分岐で
//       8 バイト値を 4 バイト slot へ STORE していた。
//   (2) build_node_num が 32bit に収まらないリテラル (10000000000L 等) も i32 で
//       生成し、後段の 64bit 拡張 (sxtw) が下位 32bit を符号拡張して上位を捨てた。
//   (3) 整数 binop (a+a 等) の結果型が常に i32 で、オペランドが long でも
//       sxtw で切り詰められた。
// 修正: ternary が long 分岐を検出して 8 バイト slot を使い両分岐を 8 バイトへ
//       拡張 / build_node_num は範囲外リテラルを i64 生成 / 整数 binop は
//       64bit オペランドなら結果も i64。
// 修正前: long 分岐が下位 32bit に化ける
// 期待: exit=42
#include <assert.h>
int main(void) {
    long r = 0;

    // (a) long リテラル分岐 (else 採択)
    int c0 = 0;
    long x = c0 ? 5L : 10000000000L;       // 10000000000 (0x2_540BE400)
    assert(x % 1000 == 0);           // 下位 32bit 化けなら 408
    r += (x == 10000000000L) ? 10 : 0;     // 10

    // (b) long 変数分岐
    long big = 9000000000L;
    int c1 = 1;
    long y = c1 ? big : 0L;
    assert(y == 9000000000L);
    r += 12;                               // 12

    // (c) long 算術を直接分岐に
    long a = 5000000000L;
    int c2 = 1;
    long z = c2 ? a + a : 0L;              // 10000000000
    assert(z == 10000000000L);
    r += 20;                               // 20

    assert(r == 42); return 0;                         // 10 + 12 + 20 = 42
}
