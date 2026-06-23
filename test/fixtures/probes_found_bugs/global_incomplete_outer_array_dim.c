// グローバル多次元配列の最外 `[]` 次元推論 + 部分行初期化 (c-testsuite 00151)。
// `int arr[][3][5] = {{...},{...}}` でネスト brace 初期化時、行 `{0,0,3,5}` の直後に
// 次行が slot 境界に揃えられず `[3]=6` 等が誤位置へ。外側次元も init_count*elem で
// 誤推論 (17 要素) していた。
// 修正: psx_gbrace_flat で sub_dims 積による positional 要素境界揃え。type_size=0 の
// 多次元は outer_stride 単位で外側次元を推論。
// 期待: exit=0
int arr[][3][5] = {
    {
        {0, 0, 3, 5},
        {1, [3] = 6, 7},
    },
    {
        {1, 2},
        {[4] = 7},
    },
};
int main(void) {
    return !(arr[0][1][4] == arr[1][1][4]);
}
