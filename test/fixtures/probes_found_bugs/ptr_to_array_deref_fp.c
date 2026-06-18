// 配列へのポインタ `double (*dp)[N]` の単項 deref + subscript 形 `(*dp)[j]` が float/double
// で整数 load になり値が化けていたバグ (local / global 共通)。`dp[i][j]` 形は別経路で動く。
// 原因: build_unary_deref_node が `*dp` (= pointee 配列の「行」) に base.fp_kind を立てて
//      いたが、行はスカラ値ではないため後続 subscript `(*dp)[j]` が要素の fp 種別を
//      参照できず (pointee_fp_kind 未設定) 整数 load していた。
// 修正: deref 結果がまだ配列/行 (deref_size>0) のとき、要素 fp 種別を pointee_fp_kind にも
//      伝播する (base.fp_kind は `_Generic(*p,...)` 等のスカラ deref のため保持)。
// 修正前: 値破損
// 期待: exit=42
double g_drows[2][2] = {{1.5, 2.5}, {3.5, 4.5}};
float  g_frows[2][2] = {{1.5f, 2.5f}, {3.5f, 4.5f}};
double (*g_dp)[2] = g_drows;
float  (*g_fp)[2] = g_frows;

int main(void){
    // グローバル pointer-to-array の (*pa)[j]
    int a = (int)((*g_dp)[1]);     // g_drows[0][1] = 2.5 -> 2
    int b = (int)((*g_fp)[0]);     // g_frows[0][0] = 1.5 -> 1

    // ローカル pointer-to-array の (*pa)[j]
    double drows[2][2] = {{10.5, 20.5}, {3.5, 4.5}};
    double (*dp)[2] = drows;
    int c = (int)((*dp)[0]);       // drows[0][0] = 10.5 -> 10
    int d = (int)((*(dp + 1))[1]); // drows[1][1] = 4.5 -> 4

    // dp[i][j] 形 (回帰確認)
    int e = (int)(dp[1][0]);       // drows[1][0] = 3.5 -> 3

    // 2 + 1 + 10 + 4 + 3 = 20; + 22 = 42
    return a + b + c + d + e + 22;
}
