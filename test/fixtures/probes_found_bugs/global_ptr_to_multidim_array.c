// 多次元 pointee の配列へのポインタ `T (*pa)[N][M]` グローバルが SIGSEGV / 誤値だった。
// (1 次元 pointee `(*pa)[N]` は別コミットで対応済み。ローカルは元から動作。)
// 原因: グローバルの pointer-to-array 対応が 1 次元 pointee 限定で、多次元 pointee の
//      mid_stride / extra_strides を設定していなかった (dim_count>=2 で配列扱いに落ち crash)。
//      加えて double では `(*pa)[j][k]` (単項 deref + 多段 subscript 形) の中間「行」に
//      pointee_fp_kind が伝播せず要素が整数 load になっていた。
// 修正: pointee 各次元を仮想先頭次元付き dims で apply_global_multidim_strides に渡して
//      outer/mid/extra を計算し、try_build_global_var_node が ND_GVAR に多段ストライドを
//      反映。build_unary_deref_node は行 deref (deref_size>0) の要素 fp 種別を pointee_fp_kind
//      に伝播 (inner_deref_size>0 の多次元行も含む)。
// 修正前: SIGSEGV / 誤値
// 期待: exit=42
#include <assert.h>
int    icube[2][2][3] = {{{1, 2, 3}, {4, 5, 6}}, {{7, 8, 9}, {10, 11, 12}}};
double dcube[2][2][2] = {{{1.5, 2.5}, {3.5, 4.5}}, {{5.5, 6.5}, {7.5, 8.5}}};
int    (*ip)[2][3] = icube;
double (*dp)[2][2] = dcube;

int main(void){
    int a = ip[1][0][2];        // icube[1][0][2] = 9
    int b = (*ip)[1][2];        // icube[0][1][2] = 6
    int c = (int)dp[1][1][0];   // dcube[1][1][0] = 7.5 -> 7
    int d = (int)(*dp)[0][1];   // dcube[0][0][1] = 2.5 -> 2
    assert(a == 9);   // ip[1][0][2]
    assert(b == 6);   // (*ip)[1][2]
    assert(c == 7);   // dp[1][1][0]
    assert(d == 2);   // (*dp)[0][1]
    return 0;
}
