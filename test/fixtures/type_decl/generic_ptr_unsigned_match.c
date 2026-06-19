// unsigned int* マッチ
// 期待: exit=0
#include <assert.h>
int main(void) {
    int x = 0;
    unsigned int u = 0;
    unsigned int *pu = &u;
    assert(_Generic(pu, int*: 1, unsigned int*: 2, default: 3) == 2);
    return 0;
}
