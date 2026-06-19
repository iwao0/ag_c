// 関数ポインタ仮引数 `int (*f)(int,int)` で 2 関数を切り替え
// apply(add,3,4)*10 + apply(mul,2,5) = 7*10 + 10 = 80
// 期待: exit=80
#include <assert.h>
int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }
int apply(int (*f)(int, int), int a, int b) { return f(a, b); }
int main(void) {
    assert(apply(add, 3, 4) == 7);
    assert(apply(mul, 2, 5) == 10);
    return 0;
}
