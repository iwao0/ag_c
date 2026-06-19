// sizeof のオペランドは評価されない (x=99 の代入は走らない)
// 期待: exit=0
#include <assert.h>
int main(void) {
    int x = 0;
    sizeof(x = 99);
    assert(x == 0);
    return 0;
}
