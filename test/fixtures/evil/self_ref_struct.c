// 自己参照ポインタを持つ struct (リンクリストノード)
// 期待: exit=42
int main(void) {
    struct Node { int val; struct Node *next; };
    struct Node n;
    n.val = 42;
    n.next = 0;
    return n.val;
}
