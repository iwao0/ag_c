// 関数ポインタ assoc で typedef 経由マッチ
// 期待: exit=0
#include <assert.h>
typedef int (*fp_t)(int);
int id(int x) { return x; }
int main(void) {
    fp_t p = id;
    assert(_Generic(p, int (*)(int): 13, default: 7) == 13);
    return 0;
}
