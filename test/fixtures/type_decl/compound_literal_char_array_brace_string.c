// `(char[]){"..."}` 複合リテラルのブレース内文字列
// s[0]='h'=104, s[4]='o'=111
// 期待: exit=0
#include <assert.h>
int main(void) {
    char *s = (char[]){ "hello" };
    assert(s[0] == 'h');
    assert(s[4] == 'o');
    return 0;
}
