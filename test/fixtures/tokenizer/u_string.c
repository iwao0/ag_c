// u"AB" は char16_t[] (2 byte per element)。s[0]='A'=65
// 期待: exit=65
#include <assert.h>
int main(void) {
    short *s = u"AB";
    assert(s[0] == 65);
    return 0;
}
