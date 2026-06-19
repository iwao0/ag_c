// 3 段ポインタ経由の書き込み ***ppp = 99 → x=99
// 期待: exit=99
#include <assert.h>
int main(void) {
    int x = 3;
    int *p = &x;
    int **pp = &p;
    int ***ppp = &pp;
    ***ppp = 99;
    assert(x == 99);
    return 0;
}
