// 配列サイズ推定: 隣接文字列リテラルの連結を含むケース
// 期待: exit=199 ('a'=97, 'f'=102)
#include <assert.h>
int main(void) {
    char s[] = "abc" "def";
    assert(s[0] == 'a');
    assert(s[1] == 'b');
    assert(s[2] == 'c');
    assert(s[3] == 'd');
    assert(s[4] == 'e');
    assert(s[5] == 'f');
    assert(s[6] == '\0');
    return 0;
}
