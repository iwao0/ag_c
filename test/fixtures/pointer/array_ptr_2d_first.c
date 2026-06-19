// 配列へのポインタ `int (*p)[3]` で 0 行目アクセス
// 期待: exit=2 (a[0][1])
#include <assert.h>
int main(void) {
    int a[2][3] = {{1,2,3}, {4,5,6}};
    int (*p)[3] = a;
    assert(p[0][1] == 2);
    return 0;
}
