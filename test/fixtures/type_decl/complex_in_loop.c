// _Complex の累算 (while ループで 3.0)
// 期待: exit=0
#include <assert.h>
int main(void) {
    _Complex double z = 0.0;
    int i = 0;
    while (i < 3) { z = z + 1.0; i = i + 1; }
    _Complex double e = 3.0;
    assert(z == e);
    return 0;
}
