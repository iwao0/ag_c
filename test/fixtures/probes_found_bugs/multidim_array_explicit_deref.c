// 多次元配列の途中次元 (行) が、明示的な単項 `*` / ポインタ算術 / subscript の
// 戻りを「値」として使うと int* へ崩壊せず値ロードして garbage になっていた。
//   int *q = m[0];   *(*(m+1)+2);   **m;   (*(m+0))[2]
// 原因 1: build_node_deref の配列崩壊判定が is_pointer=1 を要求していたが、
//         多次元途中次元は is_pointer=0。deref_size>0 && type_size>8 で判定するよう緩和。
// 原因 2: 通常多次元配列は ND_ADDR(deref_size=行, inner_deref_size=要素) 表現で、
//         build_unary_deref_node が `*m`/`*(m+k)` の内側ストライドを引き継いでいなかった。
int g[2][3] = {{1, 2, 3}, {4, 5, 6}};

int sum2(int (*a)[4], int rows) {
  int s = 0;
  for (int i = 0; i < rows; i++)
    for (int j = 0; j < 4; j++) s += a[i][j];
  return s;
}

int main(void) {
  int m[3][4];
  int v = 0;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 4; j++) m[i][j] = v++;

  int t = 0;
  int *q = m[0];          // 行が int* へ崩壊
  t += q[2];              // m[0][2] = 2
  t += *(*(m + 1) + 2);   // m[1][2] = 6
  t += **m;               // m[0][0] = 0
  t += (*(m + 0))[3];     // m[0][3] = 3
  t += sum2(m, 3);        // 0+1+...+11 = 66
  // 2D グローバル配列も同様
  t += *(*(g + 1) + 2);   // g[1][2] = 6
  t += **g;               // g[0][0] = 1

  return t - 42;          // 2+6+0+3+66+6+1 = 84 ; 84-42 = 42
}
