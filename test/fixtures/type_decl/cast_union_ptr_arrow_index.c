// union ポインタの -> で配列 0 番要素
// 期待: exit=1
#include <assert.h>
int main(void) {
    union U { int a[2]; int z; };
    union U u = {1, 2};
    assert(((union U*)&u)->a[0] == 1);
    return 0;
}
