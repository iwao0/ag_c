// 文字列リテラルへのポインタを s[1] で添字
// 'B' = 66
// 期待: exit=66
#include <assert.h>
int main(void) {
    char *s = "AB";
    assert(s[0] == 'A');
    assert(s[1] == 'B');
    assert(s[2] == '\0');
    return 0;
}
