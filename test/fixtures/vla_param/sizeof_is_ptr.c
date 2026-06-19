// 仮引数 VLA に対する sizeof はポインタサイズ (=8)
// 期待: exit=8
#include <assert.h>
int get_size(int n, int a[n]) {
    return (int)sizeof(a);
}
int main(void) {
    int n = 10;
    int a[n];
    assert(get_size(n, a) == 8);
    return 0;
}
