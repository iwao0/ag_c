// errno.h の EDOM == 33 / ENAMETOOLONG == 36
// 期待: exit=42
#include <errno.h>
#include <assert.h>
int main(void) {
    assert(EDOM == 33);
    assert(ENAMETOOLONG == 36);
    return 0;
}
