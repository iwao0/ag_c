// 2D VLA を関数に渡す
int sum_grid(int n, int m, int g[n][m]) {
  int s = 0;
  for (int i = 0; i < n; i++)
    for (int j = 0; j < m; j++) s += g[i][j];
  return s;
}
int main(void) {
  int arr[2][3] = { {1,2,3}, {4,5,6} };
  return sum_grid(2, 3, arr);
}
// 期待: 21
