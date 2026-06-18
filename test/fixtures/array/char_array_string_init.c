// char 配列の文字列リテラル初期化 (明示サイズ)
// char s[4] = "abc"; → s = {'a','b','c','\0'}
// 期待: exit=99 ('c'=99, s[3]=0)
#include <assert.h>
int main(void) {
    char s[4] = "abc";
    assert(s[0] == 'a');
    assert(s[1] == 'b');
    assert(s[2] == 'c');
    assert(s[3] == 0);
    return 0;
}
