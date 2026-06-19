// 再帰でフィボナッチ (fib(10) = 55)
// 期待: exit=55
#include <assert.h>
int fib(int n) {
    return n <= 1 ? n : fib(n-1) + fib(n-2);
}
int main(void) { assert(fib(10) == 55); return 0; }
