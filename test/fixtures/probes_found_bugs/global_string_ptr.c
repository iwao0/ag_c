// グローバル char *p = "...";
char *greet = "hello world";
int main(void) {
  int n = 0;
  while (greet[n]) n++;
  return n;  // 11
}
// 期待: 11
