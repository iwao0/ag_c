// L'A' は wchar_t 値 = 'A' = 65
// 期待: exit=65
#include <assert.h>
int main(void) {
    assert(L'A' == 65);
    return 0;
}
