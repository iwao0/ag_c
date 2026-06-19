// 2 段ポインタ仮引数の関数を 2 回呼んで元変数を更新
// 期待: exit=12
#include <assert.h>
void inc(int **pp) { (**pp)++; }
int main(void) {
    int x = 10;
    int *p = &x;
    inc(&p);
    inc(&p);
    assert(x == 12);
    return 0;
}
