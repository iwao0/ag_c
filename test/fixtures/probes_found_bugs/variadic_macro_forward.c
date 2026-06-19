// 可変長マクロ (C99/C11 `...` / __VA_ARGS__) のサポート。
// 修正前: `#define CALL(f, ...)` の定義が E1024 で拒否されコンパイル不可。
// 可変長マクロは __VA_ARGS__ を合成パラメータとして展開し、引数を実関数へ転送する。
// 期待: exit=42  (mul(6,7))
#include <assert.h>
#define CALL(f, ...) f(__VA_ARGS__)
#define FIRST(a, ...) (a)
int mul(int x, int y) { return x * y; }
int main(void) {
    int r = CALL(mul, 6, 7);   // -> mul(6, 7) = 42
    assert(r == 42); assert(FIRST(0, 99) == 0); return 0;   // FIRST -> 0
}
