// inline 指定子 (C11 6.7.4): 単一翻訳単位では通常関数と同等に動作
#include <assert.h>
inline int add(int a, int b) { return a + b; }
int main(void) {
    assert(add(20, 22) == 42);
    return 0;
}
