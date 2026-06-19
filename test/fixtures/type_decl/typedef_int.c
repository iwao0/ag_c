// 基本的な typedef int alias
// 期待: exit=9
#include <assert.h>
typedef int myint;
int main(void) {
    myint x = 9;
    assert(x == 9);
    return 0;
}
