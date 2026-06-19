// `typedef int R[3]; R *p;` は「int[3] へのポインタ」。p+1 は sizeof(int[3])=12 byte
// 進み、p[i][j] は行/要素ストライドで添字できる必要がある。ローカル宣言子が
// typedef 配列次元をポインタ pointee に反映せず、p の pointee を int(4) 扱いして
// p+1 が要素 1 個分しか進まなかった (直書き `int(*p)[3]` は正常)。
// is_pointer + td_array_dim_count 分岐で outer_stride/mid_stride を設定。
// 2D typedef 配列ポインタ `m23 *q` の深い添字 q[i][j][k] も、pointer_qual_levels を
// 立てない (= base_deref_size 単段でなく多段ストライド連鎖を使う) ことで対応。
#include <assert.h>

typedef int row3[3];
typedef int m23[2][3];

int main(void) {
    int mat[2][3] = {{1, 2, 3}, {4, 5, 6}};
    row3 *p = mat;

    // p+1 は 1 行 (3 int) ぶん進む
    int *base = (int *)p;
    int *next = (int *)(p + 1);
    assert((int)(next - base) == 3);

    // 添字: p[i][j]
    assert(p[0][0] == 1);
    assert(p[0][1] == 2);
    assert(p[0][2] == 3);
    assert(p[1][0] == 4);
    assert(p[1][1] == 5);
    assert(p[1][2] == 6);

    // p+2 で 2 行先 (範囲確認のためもう 1 行ある配列で)
    int big[3][3] = {{0, 0, 0}, {0, 0, 0}, {7, 8, 9}};
    row3 *bp = big;
    assert(bp[2][0] == 7);
    assert(bp[2][2] == 9);

    // 2D typedef 配列へのポインタ: 3 段の添字が各次元のストライドで進む
    int a[2][2][3];
    int n = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 3; k++) a[i][j][k] = n++;
    m23 *q = a;
    assert(q[0][0][0] == 0);
    assert(q[0][1][2] == 5);
    assert(q[1][0][0] == 6);   // q+1 は [2][3]=6 int ぶん
    assert(q[1][1][0] == 9);   // 第2添字は mid_stride (3 int) ぶん
    assert(q[1][1][2] == 11);
    return 0;
}
