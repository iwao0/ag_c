// 2 段ポインタ経由のインクリメント (**pp)++
// 期待: exit=8
#include <assert.h>
int main(void) {
    int x = 7;
    int *p = &x;
    int **pp = &p;
    (**pp)++;
    assert(x == 8);
    return 0;
}
