// 配列複合リテラルの 0 番要素
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(((int[3]){10, 20, 30})[0] == 10);
    return 0;
}
