// 配列複合リテラルの 2 番要素
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(((int[3]){10, 20, 30})[2] == 30);
    return 0;
}
