// 複合リテラル (T[]){[N]=...} 指定初期化子
// p[0]=1, p[3]=10, p[5]=100
// 期待: exit=0
#include <assert.h>
int main(void) {
    int *p = (int[]){ [3] = 10, [0] = 1, [5] = 100 };
    assert(p[0] == 1);
    assert(p[3] == 10);
    assert(p[5] == 100);
    return 0;
}
