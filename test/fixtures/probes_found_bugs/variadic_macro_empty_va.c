// 可変長マクロの __VA_ARGS__ は空でもよい (clang/gcc 互換、C23 で標準化)。
// `F(a, ...)` を `F(42)` で、`G(...)` を `G()` で呼べる。preprocess.c の呼び出し側
// 引数チェックが `parsed_args <= num_named` で空 va を拒否 (E1024) していたのを
// `< num_named` に緩め、名前付き引数が不足する場合のみエラーにする。
#include <assert.h>

#define FIRST(a, ...) (a)
#define COUNT0(...) 0
#define WRAP(...) (0 __VA_ARGS__)   // 空なら (0)、非空なら (0 ,x) ... ここでは空のみ使う

int main(void) {
    assert(FIRST(42) == 42);          // 空 __VA_ARGS__
    assert(FIRST(42, 1, 2) == 42);    // 非空 __VA_ARGS__
    assert(COUNT0() == 0);            // 全可変長を空で呼ぶ
    assert(COUNT0(1, 2, 3) == 0);
    assert(WRAP() == 0);              // 空 __VA_ARGS__ を本体で展開
    return 0;
}
