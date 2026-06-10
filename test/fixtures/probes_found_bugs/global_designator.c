// グローバル配列の sparse designator
int g[10] = { [0]=1, [3]=8, [9]=21 };
int main(void) {
  return g[0] + g[3] + g[5] + g[9];
}
// 期待: 1 + 8 + 0 + 21 = 30
