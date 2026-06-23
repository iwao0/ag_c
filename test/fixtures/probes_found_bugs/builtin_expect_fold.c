// c-testsuite 00214: __builtin_expect(exp, c) は exp に畳み込む (外部シンボル不要)。
extern int printf(const char *, ...);

static int fold(int x) {
  if (__builtin_expect(!!(x == 0), 0))
    return 0;
  return x;
}

int main(void) {
  if (__builtin_expect(!!(fold(1) == 1), 1))
    printf("okay\n");
  else
    printf("wrong\n");
  return 0;
}
