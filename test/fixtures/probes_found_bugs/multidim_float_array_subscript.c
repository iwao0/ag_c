// 多次元 float/double 配列の subscript `m[i][j]` が整数 load で読まれていた。
// build_subscript_deref が float 配列の subscript 結果に fp_kind を base.fp_kind で
// 載せていたため、多次元の 1 段目 (まだ配列) の結果が「float 値」扱いになり、次段
// subscript が pointee の fp 種別を見失って整数 load していた。
// 中間結果 (es > inner_ds) は pointee_fp_kind に伝播し、最終要素のみ base.fp_kind に
// する (is_bool と同じ分岐)。`float *a` 仮引数は inner_ds=elem が立つので es>inner_ds
// で多次元の中間かどうかを区別する。
float sum2d(float (*g)[3], int rows){
  float s = 0;
  for (int i = 0; i < rows; i++) for (int j = 0; j < 3; j++) s += g[i][j];
  return s;
}
float sum1d(float *a, int n){ float s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }

int main(void) {
  int t = 0;

  // 2D float 配列 (brace 初期化 + 読み出し)
  float m[2][3] = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
  float ms = 0;
  for (int i = 0; i < 2; i++) for (int j = 0; j < 3; j++) ms += m[i][j];
  t += (ms == 21.0f);
  t += (m[1][1] == 5.0f);
  t += ((int)m[0][2] == 3);

  // 3D float
  float cube[2][2][2] = {{{1,2},{3,4}},{{5,6},{7,8}}};
  t += (cube[1][1][1] == 8.0f);

  // 1D float 配列 / float* 仮引数 (回帰確認)
  float arr[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  t += (arr[2] == 3.0f);
  t += (sum1d(arr, 4) == 10.0f);

  // 2D float 配列を関数へ
  t += (sum2d(m, 2) == 21.0f);

  // double 2D
  double dg[2][2] = {{1.5, 2.5}, {3.5, 4.5}};
  t += (dg[1][0] == 3.5);

  return t + 34;  // 8 checks -> 8+34 = 42
}
