// typedef 多次元配列に初期化子リストで値を入れる。
// m[1][2] = 6。
// 期待: exit=0
#include <assert.h>
typedef int M2[2][3];
int main(void) {
    M2 m = {{1, 2, 3}, {4, 5, 6}};
    assert(m[1][2] == 6);
    return 0;
}
