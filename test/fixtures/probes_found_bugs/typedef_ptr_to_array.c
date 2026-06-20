// typedef 自体が「配列へのポインタ」`typedef int (*PA)[3];` のとき、その typedef で
// 宣言した変数のポインタ算術 (p+1 / p[i]) が 1 行ぶん (3 int = 12B) でなく要素 1 個
// (4B) しか進まず、直書き `int (*p)[3]` と挙動が食い違っていた。
//   原因: typedef 定義 (toplevel: parser.c / local: stmt.c) が is_ptr のときポインティ
//   配列の extent [3] を捨てていた。pointer-to-array typedef では `*` が括弧内
//   (ptr_in_paren) のとき後続の `[3]` をポインティ dims として記録し、変数宣言側の
//   `is_pointer && td_array_dim_count>0` 分岐が outer_stride / mid_stride を設定する。
//   resolve_typedef_array_dims の is_array ゲートも外した (pointer-to-array は is_array=0)。
//   既存の `typedef int R[3]; R *p` (配列 typedef へのポインタ) とは別経路。
#include <assert.h>

typedef int (*PA)[3];        // 1 次元ポインティ
typedef int (*PB)[2][3];     // 多次元ポインティ

int main(void) {
    int m[2][3] = {{1, 2, 3}, {4, 5, 6}};

    // toplevel typedef を局所変数で使用
    PA p = m;
    assert(p[0][0] == 1 && p[0][2] == 3);
    assert(p[1][0] == 4 && p[1][2] == 6);   // 行送りが効くこと
    assert((*p)[1] == 2);
    assert((*(p + 1))[1] == 5);             // p+1 が 1 行進むこと

    // 多次元ポインティ
    int c[2][2][3] = {{{1, 2, 3}, {4, 5, 6}}, {{7, 8, 9}, {10, 11, 12}}};
    PB q = c;
    assert(q[1][0][2] == 9 && q[0][1][1] == 5);

    // 関数ローカルで定義した typedef (stmt.c 経路)
    typedef int (*LA)[3];
    LA r = m;
    assert(r[1][2] == 6 && (*(r + 1))[0] == 4);

    return 0;
}
