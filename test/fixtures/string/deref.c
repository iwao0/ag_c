// 文字列リテラルへのポインタを *s でデリファレンス
// 'A' = 65
// 期待: exit=65
#include <assert.h>
int main(void) {
    char *s = "AB";
    assert(*s == 'A');
    assert(s[0] == 'A');
    assert(s[1] == 'B');
    assert(s[2] == '\0');
    return 0;
}
