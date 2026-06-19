// `char const c` (後置 const 修飾)
// 期待: exit=65 ('A')
#include <assert.h>
int main(void) {
    char const c = 65;
    assert(c == 65);
    return 0;
}
