// case から switch 外のラベルへ goto
// 期待: exit=42
#include <assert.h>
int main(void) {
    int x = 1;
    int result = 0;
    switch (x) {
        case 1: goto done;
        case 2: result = 20; goto end;
        default: result = 99; goto end;
    }
done:
    result = 42;
end:
    assert(result == 42);
    return 0;
}
