// c-testsuite 00209: 未完了タグのポインタ宣言と `int (int x)` 形の関数型仮引数。
// 修正前: enum E *e で E 未定義 E3066 / enum E const *e2 で const 直後に E3016 /
// int f(int (int x), int) で `(int x)` を declarator と誤解し E2006。
#include <assert.h>

enum E *e;
enum E const *e2;
struct S *s;

typedef int (*fptr2)(int x);
int f2(int (int x), int);
static int inc(int x) { return x + 1; }
int f2(fptr2 fp, int i) { return fp(i); }

int main(void) {
  assert(f2(inc, 41) == 42);
  return 0;
}
