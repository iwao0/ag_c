// 配列サイズ推定: char s[] = "hello"
// 修正前: E3064 "数値が必要です ']'" でコンパイル失敗
// 期待: exit=215 ('h'=104, 'o'=111)
#include <assert.h>
int main(void) {
    char s[] = "hello";
    assert(s[0] == 'h');
    assert(s[1] == 'e');
    assert(s[2] == 'l');
    assert(s[3] == 'l');
    assert(s[4] == 'o');
    assert(s[5] == '\0');
    return 0;
}
