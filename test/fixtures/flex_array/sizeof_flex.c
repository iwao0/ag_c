// flexible array member (`int data[]`) は sizeof に含まれない
// `struct F { int len; int data[]; }` の sizeof は len (4 byte) のみ
// 期待: exit=4
#include <assert.h>
int main(void) {
    struct F { int len; int data[]; };
    assert((int)sizeof(struct F) == 4);
    return 0;
}
