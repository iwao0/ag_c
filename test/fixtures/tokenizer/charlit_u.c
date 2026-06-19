// u'A' は char16_t 値 = 'A' = 65
// 期待: exit=65
#include <assert.h>
int main(void) {
    assert(u'A' == 65);
    return 0;
}
