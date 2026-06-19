// flexible array member への malloc 後の書き込み・読み出し
// 期待: exit=42 (10+20+12)
#include <stdlib.h>
#include <assert.h>
struct F { int len; int data[]; };
int main(void) {
    struct F *f = malloc(sizeof(struct F) + 3 * sizeof(int));
    f->len = 3;
    f->data[0] = 10;
    f->data[1] = 20;
    f->data[2] = 12;
    assert(f->data[0] == 10);
    assert(f->data[1] == 20);
    assert(f->data[2] == 12);
    free(f);
    return 0;
}
