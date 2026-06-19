// string.h の strcmp で等値判定
// 期待: exit=42
#include <string.h>
#include <assert.h>
int main(void) {
    assert(strcmp("abc", "abc") == 0);
    return 0;
}
