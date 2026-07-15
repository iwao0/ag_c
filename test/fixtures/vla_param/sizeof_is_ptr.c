// 仮引数 VLA に対する sizeof は target pointer size
#include <assert.h>
int get_size(int n, int a[n]) {
    return (int)sizeof(a);
}
int main(void) {
    int n = 10;
    int a[n];
    assert(get_size(n, a) == sizeof(void*));
    return 0;
}
