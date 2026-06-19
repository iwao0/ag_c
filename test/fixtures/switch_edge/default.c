// switch の default 分岐
// 期待: exit=30
#include <assert.h>
int main(void) {
    int a = 9;
    int result;
    switch (a) {
        case 1: result = 10; break;
        case 2: result = 20; break;
        default: result = 30; break;
    }
    assert(result == 30);
    return 0;
}
