// 2 段ポインタを使った swap
// swap 後 *px=20, *py=10
// 期待: exit=120
#include <assert.h>
void swap(int **a, int **b) {
    int *t = *a;
    *a = *b;
    *b = t;
}
int main(void) {
    int x = 10;
    int y = 20;
    int *px = &x;
    int *py = &y;
    swap(&px, &py);
    assert(*px == 20);
    assert(*py == 10);
    return 0;
}
