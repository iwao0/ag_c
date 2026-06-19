// typedef 多次元配列の sizeof は全要素のバイト数を返す。
// M3 = int[2][3][4] なので sizeof(M3) = 2*3*4*4 = 96 byte。
// 期待: exit=0
#include <assert.h>
typedef int M3[2][3][4];
int main(void) {
    assert(sizeof(M3) == 96);
    return 0;
}
