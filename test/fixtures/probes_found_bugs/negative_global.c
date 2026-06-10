// グローバル変数の負数初期化
int g = -42;
int *gp = (int *)0;
int main(void) {
  return -g + (gp == 0 ? 0 : 100);
}
// 期待: 42
