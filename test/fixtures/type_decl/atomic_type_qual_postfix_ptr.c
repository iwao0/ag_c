// int _Atomic * (atomic int へのポインタ)
// 期待: exit=0
#include <assert.h>
int main(void) {
    int _Atomic x = 7;
    int _Atomic *p = &x;
    assert(*p == 7);
    return 0;
}
