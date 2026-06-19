// union のネスト指定 .a[1]=3
// 期待: exit=3
#include <assert.h>
int main(void) {
    union U { int a[2]; int z; };
    union U u = {.a[1] = 3};
    assert(u.a[1] == 3);
    return 0;
}
