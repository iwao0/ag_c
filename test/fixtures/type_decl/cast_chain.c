// 型キャストのチェーン
// 期待: exit=42
#include <assert.h>
int main(void) {
    assert((int)(char)(short)(long)42 == 42);
    return 0;
}
