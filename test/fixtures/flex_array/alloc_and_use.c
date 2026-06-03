// flexible array member への malloc 後の書き込み・読み出し
// 期待: exit=42 (10+20+12)
#include <stdlib.h>
struct F { int len; int data[]; };
int main(void) {
    struct F *f = malloc(sizeof(struct F) + 3 * sizeof(int));
    f->len = 3;
    f->data[0] = 10;
    f->data[1] = 20;
    f->data[2] = 12;
    int s = f->data[0] + f->data[1] + f->data[2];
    free(f);
    return s;
}
