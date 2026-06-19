// 関数内 typedef
// 期待: exit=6
#include <assert.h>
int main(void) {
    typedef int myint;
    myint x = 6;
    assert(x == 6);
    return 0;
}
