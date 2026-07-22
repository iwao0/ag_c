// static inline 関数も通常の関数と同じくアドレスを取得して間接呼び出しできる。
#include <assert.h>

static inline int twice(int value) {
    return value * 2;
}

int main(void) {
    int (*operation)(int) = twice;
    assert(operation(21) == 42);
    return 0;
}
