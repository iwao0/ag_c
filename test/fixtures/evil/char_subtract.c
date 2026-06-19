// 文字リテラルの差で '9'-'0' = 9
// 期待: exit=9
#include <assert.h>
int main(void) {
    char a = '0';
    char b = '9';
    assert(b - a == 9);
    return 0;
}
