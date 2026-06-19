// _Static_assert で sizeof(int) == 4 を確認
// 期待: exit=0
#include <assert.h>
int main(void) {
    _Static_assert(sizeof(int) == 4, "int is 4");
    return 0;
}
