// u8"ABC" は UTF-8 char[]。s[0]='A'=65, s[2]='C'=67 → 132
// 期待: exit=132
#include <assert.h>
int main(void) {
    char *s = u8"ABC";
    assert(s[0] == 65);
    assert(s[2] == 67);
    return 0;
}
