// limits.h の CHAR_BIT は 8
// 期待: exit=42
#include <limits.h>
#include <assert.h>
int main(void) {
    assert(CHAR_BIT == 8);
    return 0;
}
