// ポインタ配列経由の書き込み (同じ変数を 2 か所から)
// 期待: exit=15 (10+5)
#include <assert.h>
int main(void) {
    int x = 0;
    int *ptrs[2];
    ptrs[0] = &x;
    ptrs[1] = &x;
    *ptrs[0] = 10;
    *ptrs[1] = *ptrs[1] + 5;
    assert(x == 15);
    return 0;
}
