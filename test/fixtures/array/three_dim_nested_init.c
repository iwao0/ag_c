// 3 次元配列のネスト初期化と添字アクセス
// 修正前: 添字 `a[i][j][k]` の codegen が壊れていた:
//        1) `outer_stride = inner_dim * elem` (2D 用) のままで、3D の最外側
//           ストライドが N3 1 つ分しか進まなかった
//        2) ネスト初期化 `{{{...},{...}}, ...}` を parse_array_initializer が
//           扱えなかった (2 段までしか対応)
// 対応:
// - canonical array type と expression-local type_state で、サブスクリプト
//   チェーンの「1段先」「2段先」のストライドを伝搬
// - parse_array_initializer に sub_row_len による中段 `{...}` の再帰処理を追加
// 期待: exit=12
#include <assert.h>
int main(void) {
    int a[2][2][3] = {{{1,2,3},{4,5,6}}, {{7,8,9},{10,11,12}}};
    assert(a[0][0][0] == 1);
    assert(a[0][0][1] == 2);
    assert(a[0][0][2] == 3);
    assert(a[0][1][0] == 4);
    assert(a[0][1][1] == 5);
    assert(a[0][1][2] == 6);
    assert(a[1][0][0] == 7);
    assert(a[1][0][1] == 8);
    assert(a[1][0][2] == 9);
    assert(a[1][1][0] == 10);
    assert(a[1][1][1] == 11);
    assert(a[1][1][2] == 12);
    return 0;
}
