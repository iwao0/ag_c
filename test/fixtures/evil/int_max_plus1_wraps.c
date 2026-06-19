// signed int オーバーフロー (INT_MAX+1 が負になる)
// 期待: exit=1
#include <assert.h>
int main(void) {
    int x = 2147483647;
    x = x + 1;
    assert(x == -2147483648);
    assert(x < 0);
    return 0;
}
