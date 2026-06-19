// char 配列複合リテラル (要素列指定)
// 'a'=97, 'b'=98, sum=195
// 期待: exit=0
#include <assert.h>
int main(void) {
    char *s = (char[]){'a', 'b', 'c', '\0'};
    assert(s[0] == 'a');
    assert(s[1] == 'b');
    assert(s[0] + s[1] == 195);
    return 0;
}
