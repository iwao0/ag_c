// unsigned short に 200 → 200
// 期待: exit=200
#include <assert.h>
int main(void) {
    unsigned short s = 200;
    assert(s == 200);
    return 0;
}
