// 文字列ポインタ配列
const char *names[] = {"alpha", "beta", "gamma"};
int len(const char *s) { int n = 0; while (*s++) n++; return n; }
int main(void) {
  return len(names[0]) + len(names[1]) + len(names[2]);  // 5+4+5 = 14
}
// 期待: 14
