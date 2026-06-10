// 文字列を関数引数で渡す
int len(const char *s) { int n=0; while (*s++) n++; return n; }
int main(void) {
  return len("foobar");
}
// 期待: 6
