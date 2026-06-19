// ctype.h の toupper ('a' → 'A' = 65)
// 期待: exit=65
#include <ctype.h>
#include <assert.h>
int main(void) {
    assert(toupper('a') == 65);
    return 0;
}
