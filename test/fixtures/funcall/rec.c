// 再帰関数 (階乗)
// 5! = 120
// 期待: exit=120
#include <assert.h>
int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }
int main() { assert(fact(5) == 120); return 0; }
