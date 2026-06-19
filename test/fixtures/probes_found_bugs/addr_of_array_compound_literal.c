// `&(int[N]){...}` (sized array 複合リテラルのアドレス) は pointer-to-array を返す。
// 配列複合リテラルは式中で COMMA(init, ADDR(lvar)) へ decay するため、単項 & が
// rhs (既に ADDR) を二重に ADDR でラップし ir_build_module failed になっていた。
// build_unary_addr_node を rhs に再帰適用し、ND_ADDR 簡約を効かせて修正。
// scalar / struct 複合リテラルの & は元から動作 (退行が無いことも確認)。
#include <assert.h>

struct P { int x; int y; };

int main(void) {
    int (*p1)[1] = &(int[1]){99};
    assert((*p1)[0] == 99);

    int (*p3)[3] = &(int[3]){7, 8, 9};
    assert((*p3)[0] == 7);
    assert((*p3)[1] == 8);
    assert((*p3)[2] == 9);

    // 退行確認: scalar / struct 複合リテラルの &
    int *ps = &(int){42};
    assert(*ps == 42);

    struct P *pp = &(struct P){3, 4};
    assert(pp->x == 3);
    assert(pp->y == 4);

    // 退行確認: 通常配列の &
    int arr[3] = {1, 2, 3};
    int (*pa)[3] = &arr;
    assert((*pa)[0] == 1);
    assert((*pa)[2] == 3);
    return 0;
}
