// 指定初期化子 + 添字 [2]=99
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(((int[4]){[2] = 99})[2] == 99);
    return 0;
}
