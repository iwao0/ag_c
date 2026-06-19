// 明示形「配列へのポインタ」仮引数: int (*a)[N]
// 修正前: `int a[][N]` (decay 経路) は対応していたが、明示形
//        `int (*a)[N]` は内側 `[N]` が捕捉されず単純 int* 扱いになり、
//        a[i][j] が誤った要素を読んでいた (exit=4)
// 対応: parse_param_declarator_name_recursive で「ポインタが括弧内で
//      適用されたか」(paren_made_pointer) を追跡し、その場合は最初の
//      bracket もデケイさせず pointee dim として捕捉する。
// 期待: exit=6 (b[1][2])
#include <assert.h>
int get(int (*a)[3], int i, int j) {
    return a[i][j];
}
int main(void) {
    int b[2][3] = {{1,2,3}, {4,5,6}};
    assert(get(b, 0, 0) == 1);
    assert(get(b, 0, 1) == 2);
    assert(get(b, 0, 2) == 3);
    assert(get(b, 1, 0) == 4);
    assert(get(b, 1, 1) == 5);
    assert(get(b, 1, 2) == 6);
    return 0;
}
