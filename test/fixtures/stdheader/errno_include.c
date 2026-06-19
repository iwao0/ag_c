// errno.h の EDOM == 33
// 期待: exit=42
#include <errno.h>
#include <assert.h>
int main(void) {
    assert(EDOM == 33);
    return 0;
}
