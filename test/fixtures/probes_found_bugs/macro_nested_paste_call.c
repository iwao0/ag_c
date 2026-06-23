// `CAT(A,B)(x)` 形: 二段マクロ + ## paste のあと直後の `(...)` で別マクロを呼ぶ。
// ストリーム経路で `)(` 後続がバラけ AB (x) になり、かつ外側 CAT の hideset が
// 内側 CAT(x,y) をブロックして paste まで届かなかった。c-testsuite 00201 相当。
#include <assert.h>
#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
#define AB(x) CAT(x, y)
int xy = 42;
int main(void) {
    assert(CAT(A, B)(x) == 42);
    return 0;
}
