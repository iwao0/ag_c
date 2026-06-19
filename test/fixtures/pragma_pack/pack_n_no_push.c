// `#pragma pack(N)` (push 無し) と `#pragma pack()` (リセット) も動くこと
// 期待: exit=5
#pragma pack(1)
struct S { char a; int b; };
#pragma pack()
#include <assert.h>
int main(void) {
    assert((int)sizeof(struct S) == 5);
    return 0;
}
