// `(char[10]){"hi"}` の複合リテラル
// 'h'=104, 'i'=105
// 期待: exit=0
#include <assert.h>
int main(void) {
    char *s = (char[10]){"hi"};
    assert(s[0] == 'h');
    assert(s[1] == 'i');
    assert(s[0] + s[1] == 209);
    return 0;
}
