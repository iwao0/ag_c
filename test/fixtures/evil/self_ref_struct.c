// 自己参照ポインタを持つ struct (リンクリストノード)
// 期待: exit=42
#include <assert.h>
int main(void) {
    struct Node { int val; struct Node *next; };
    struct Node n;
    n.val = 42;
    n.next = 0;
    assert(n.val == 42);
    return 0;
}
