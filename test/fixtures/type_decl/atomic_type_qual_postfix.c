// int _Atomic (後置修飾子)
// 期待: exit=0
#include <assert.h>
int main(void) {
    int _Atomic x = 6;
    assert(x == 6);
    return 0;
}
