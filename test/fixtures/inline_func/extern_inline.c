// extern inline 関数の呼び出し
#include <assert.h>
extern inline int sub(int a, int b) { return a - b; }
int main(void) {
    assert(sub(50, 8) == 42);
    return 0;
}
