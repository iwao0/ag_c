// case ラベルに定数式 (1+2)
// 期待: exit=33
#include <assert.h>
int main(void) {
    int a = 3;
    int result;
    switch (a) {
        case 1+2: result = 33; break;
        default: result = 0; break;
    }
    assert(result == 33);
    return 0;
}
