// 空文字列リテラルは NUL のみ
// 期待: exit=0
#include <assert.h>
int main(void) {
    char *s = "";
    assert(*s == '\0');
    assert(s[0] == '\0');
    return 0;
}
