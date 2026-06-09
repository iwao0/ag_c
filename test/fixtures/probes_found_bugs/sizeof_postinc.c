// sizeof(int_expr++) は 4 (n++ の型は int rvalue)
int main(void) {
  int n = 0;
  return (int)sizeof(n++);
}
// 期待: 4
