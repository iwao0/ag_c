// `int **m` の二重サブスクリプト `m[i][j]` が、2 段目で誤ってポインタ算術
// (4 倍スケール) されていたバグ (セッション前から存在)。
// 原因: build_subscript_deref が subscript 結果の base_deref_size を常に
//   base のそれ (int** なら 4) で伝播していた。`m[i]` は int* (pointee=int) で
//   内側ポインタを持たないので base_deref_size=0 のはずが 4 になり、`m[i][j]` の
//   subscript が「要素はポインタ」(bds>0) と誤判定して結果を 4 倍した。
// 修正: subscript 結果が単段ポインタ (result_pql==1) なら base_deref_size=0、
//   多段のまま (result_pql>=2) のときだけ内側 scalar size を保つ。
// 修正前: int** / int*[] の 2 次元アクセスが誤値 (3+6 が 3+24=27)
// 期待: exit=42
int get(int **m, int i, int j) { return m[i][j]; }

int g0[2] = {3, 4};
int g1[2] = {5, 6};
int *grid[2] = {g0, g1};
int **getgrid(void) { return grid; }

int main(void) {
    // (1) ローカル int*[] を int** として 2 次元アクセス
    int r0[3] = {1, 2, 3}, r1[3] = {4, 5, 6};
    int *rows[2] = {r0, r1};
    if (rows[1][2] != 6 || rows[0][1] != 2) return 1;      // 配列ポインタの 2D

    // (2) int** 仮引数経由
    if (get(rows, 1, 0) != 4) return 2;

    // (3) int** を返す関数の結果を 2 次元アクセス
    int **m = getgrid();
    if (m[0][0] != 3 || m[1][1] != 6) return 3;
    int s = m[0][0] + m[1][1];                              // 3 + 6 = 9 (修正前 27)
    if (s != 9) return 4;

    // (4) int** 経由の書き込み
    int w0[2], w1[2];
    int *wm[2] = {w0, w1};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            wm[i][j] = i * 2 + j;                          // 0,1,2,3
    if (wm[1][1] != 3 || wm[0][1] != 1) return 5;

    return s + rows[1][2] + wm[1][1] + 24;                 // 9 + 6 + 3 + 24 = 42
}
