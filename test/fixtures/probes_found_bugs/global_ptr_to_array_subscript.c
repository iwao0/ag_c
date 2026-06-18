// 配列へのポインタ `T (*pa)[N]` グローバルの subscript が壊れていたバグ
// (`pa[i][j]` / `(*pa)[j]`)。int/double 両方で誤値または SIGSEGV になっていた。
// 原因:
//  (1) `int (*pa)[3]` が `int *pa[3]` (ポインタの配列) と同一に登録され、pa が「N 要素
//      配列」と誤解釈。subscript が pa のポインタ値をロードせず &pa を base にしていた。
//  (2) make_subscript_scaled_offset が ND_GVAR から inner_deref_size を読まず (ND_LVAR /
//      ND_DEREF のみ対応)、第1subscript `pa[i]` の結果が第2subscript 用の要素ストライドを
//      失い、`pa[i][j]` が行ストライドのまま誤ロード/クラッシュしていた。
// 修正:
//  (1) `*` が括弧内 (ptr_in_paren_group) + 外側 `[N]` の形を pointer-to-array と判定し、
//      スカラポインタ (is_array=0) として登録、pointee row を outer_stride に記録。
//  (2) make_subscript_scaled_offset の inner_deref_size 取得分岐に ND_GVAR を追加。
//  try_build_global_var_node のスカラ分岐が outer_stride/inner_deref_size を node に反映。
// 修正前: 誤値 / SIGSEGV
// 期待: exit=42
// 補足: double の `(*dp)[j]` (単項 deref + subscript 形) の fp load は別経路
//      (build_unary_deref_node) の未対応で本修正外。`dp[i][j]` 形は動く。
#include <assert.h>
int    irows[2][3] = {{1, 2, 3}, {4, 5, 6}};
double drows[2][2] = {{1.5, 2.5}, {3.5, 4.5}};
int    (*ip)[3] = irows;
double (*dp)[2] = drows;

// array-of-pointers (区別対象: pointer-to-array と誤判定されないこと)
int va = 7, vb = 9;
int *qa[2] = {&va, &vb};

int main(void){
    int a = ip[1][1];          // irows[1][1] = 5
    int b = (*ip)[2];          // irows[0][2] = 3
    int c = (int)dp[1][0];     // drows[1][0] = 3.5 -> 3
    int d = (int)dp[0][1];     // drows[0][1] = 2.5 -> 2
    int e = *qa[0] + *qa[1];   // 7 + 9 = 16
    assert(a == 5);    // ip[1][1]
    assert(b == 3);    // (*ip)[2]
    assert(c == 3);    // dp[1][0]
    assert(d == 2);    // dp[0][1]
    assert(e == 16);   // *qa[0] + *qa[1]
    return 0;
}
