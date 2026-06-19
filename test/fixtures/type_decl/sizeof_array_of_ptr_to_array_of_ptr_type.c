// 冗長括弧パターンの sizeof = 16
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(sizeof(int (*(*[2])[3])) == 16);
    return 0;
}
