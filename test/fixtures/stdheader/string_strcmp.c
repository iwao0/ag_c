// string.h の strcmp で等値判定
// 期待: exit=42
#include <string.h>
int main(void) {
    return strcmp("abc", "abc") == 0 ? 42 : 0;
}
