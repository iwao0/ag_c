// ctype.h の isalpha
// 期待: exit=42
#include <ctype.h>
#include <assert.h>
int main(void) {
    assert(isalpha('A') != 0);
    return 0;
}
