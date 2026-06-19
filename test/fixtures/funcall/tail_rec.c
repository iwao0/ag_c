// 末尾再帰 (1..10 の総和)
// 期待: exit=55
#include <assert.h>
int sum(int n, int acc) {
    if (n <= 0) return acc;
    return sum(n - 1, acc + n);
}
int main(void) {
    assert(sum(10, 0) == 55);
    return 0;
}
