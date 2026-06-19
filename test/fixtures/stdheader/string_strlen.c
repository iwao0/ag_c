// string.h の strlen
// 期待: exit=5
#include <string.h>
#include <assert.h>
int main(void) {
    assert((int)strlen("hello") == 5);
    return 0;
}
