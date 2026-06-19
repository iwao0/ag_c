// case 内から同じ switch 内の別ラベルへ goto
// 期待: exit=30
#include <assert.h>
int main(void) {
    int x = 2;
    int result = 0;
    switch (x) {
        case 1: result = 10; goto end;
        case 2: goto skip; result = 20;
skip:   result = 30; goto end;
        default: result = 99; goto end;
    }
end:
    assert(result == 30);
    return 0;
}
