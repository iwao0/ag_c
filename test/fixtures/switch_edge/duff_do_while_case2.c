// case 2 から do-while の途中に飛び込む
// 期待: exit=20
#include <assert.h>
int main(void) {
    int x = 0;
    switch (2) {
        case 1: do { x = 10; case 2: x = 20; } while (0);
    }
    assert(x == 20);
    return 0;
}
