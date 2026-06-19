// 配列複合リテラルの添字
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(((int[2]){1, 2})[1] == 2);
    return 0;
}
