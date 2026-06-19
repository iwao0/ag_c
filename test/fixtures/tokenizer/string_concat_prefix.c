// `u8"AB" "CD"` の隣接連結 → "ABCD"。s[0]+s[3] = 65+68 = 133
// 期待: exit=133
#include <assert.h>
int main(void) {
    char *s = u8"AB" "CD";
    assert(s[0] == 65);
    assert(s[3] == 68);
    return 0;
}
