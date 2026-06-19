// U"AB" は char32_t[] (4 byte per element)。*s='A'=65
// 期待: exit=65
#include <assert.h>
int main(void) {
    int *s = U"AB";
    assert(*s == 65);
    return 0;
}
