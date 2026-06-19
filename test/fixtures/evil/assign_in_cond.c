// if 条件内での代入式 (x = 5 → x が真)
// 期待: exit=5
#include <assert.h>
int main(void) {
    int x;
    if (x = 5) {
        assert(x == 5);
        return 0;
    }
    return 0;
}
