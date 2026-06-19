// (enum E)42 → 42 (受理確認)
// 期待: exit=42
#include <assert.h>
int main(void) {
    enum E { A = 1 };
    assert((enum E)42 == 42);
    return 0;
}
