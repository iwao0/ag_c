// ctype.h の isdigit
// 期待: exit=42
#include <ctype.h>
#include <assert.h>
int main(void) {
    assert(isdigit('5') != 0);
    return 0;
}
