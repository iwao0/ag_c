#include <assert.h>

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }

int main(void) {
    typedef int (*BinOp)(int, int);
    typedef BinOp Ops[2];

    Ops ops = {add, sub};
    Ops matrix[2];
    matrix[0][0] = add;
    matrix[0][1] = sub;
    matrix[1][0] = sub;
    matrix[1][1] = add;
    Ops *p = &ops;
    assert((*p)[0](7, 5) == 12);
    assert((*p)[1](7, 5) == 2);
    assert(matrix[1][0](9, 4) == 5);
    assert(matrix[1][1](9, 4) == 13);
    return 0;
}
