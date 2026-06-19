// L"AB" は wchar_t[] (4 byte per element)。 (int*)cast の先頭で 'A'=65
// 期待: exit=65
#include <assert.h>
int main(void) {
    int *p = (int *)L"AB";
    assert((*p & 0xff) == 65);
    return 0;
}
