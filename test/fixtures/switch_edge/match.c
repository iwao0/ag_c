// switch の case マッチ
// 期待: exit=20
#include <assert.h>
int main(void) {
    int a = 2;
    int result;
    switch (a) {
        case 1: result = 10; break;
        case 2: result = 20; break;
        default: result = 30; break;
    }
    assert(result == 20);
    return 0;
}
