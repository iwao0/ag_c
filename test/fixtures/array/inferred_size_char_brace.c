// 配列サイズ推定: char a[] = {'h','i','\0'}
// 修正前: E3064 "数値が必要です ']'" でコンパイル失敗
// 期待: exit=209 ('h'=104, 'i'=105)
#include <assert.h>
int main(void) {
    char a[] = {'h', 'i', '\0'};
    assert(a[0] == 'h');
    assert(a[1] == 'i');
    assert(a[2] == '\0');
    return 0;
}
