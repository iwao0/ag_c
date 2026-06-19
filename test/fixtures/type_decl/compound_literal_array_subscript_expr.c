// 2 つの複合リテラルの値を加算 (3+4=7)
// 期待: exit=0
#include <assert.h>
int main(void) {
    assert(((int[2]){3, 4})[0] + ((int[2]){3, 4})[1] == 7);
    return 0;
}
