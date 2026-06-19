// 深いネスト型の assoc (パース確認、マッチしない)
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(_Generic((int (*(*)(void))[3])0, int (*(*)(void))[3]: 1, default: 2) == 2);
    return 0;
}
