// 深いネスト = 8
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(sizeof(int (*(*(*)(void))(int))[3]) == sizeof(void*));
    return 0;
}
