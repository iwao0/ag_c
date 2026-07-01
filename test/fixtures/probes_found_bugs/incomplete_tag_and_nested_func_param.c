// c-testsuite 00209: 未完了タグのポインタ宣言と `int ()` / `int (int x)` 形の関数型仮引数。
// 修正前: enum E *e で E 未定義 E3066 / enum E const *e2 で const 直後に E3016 /
// int f(int (int x), int) で `(int x)` を declarator と誤解し E2006。
// 回帰: `int ()` の空括弧 abstract function declarator を unnamed int として記録し、
// `int f1(int (), int);` と `int f1(fptr1, int)` が conflicting types になっていた。
#include <assert.h>

enum E *e;
const enum E *e1;
enum E const *e2;
struct S *s;
const struct S *s1;
struct S const *s2;

typedef int (*fptr1)();
int f1(int (), int);
typedef int (*fptr2)(int x);
int f2(int (int x), int);
typedef int (*fptr3)(int);
int f3(int (int), int);
typedef int (*fptr4[4])(int);
int f4(int (*[4])(int), int);
typedef int (*fptr5)(fptr1);
int f5(int (int()), fptr1);

static int id(int x) { return x; }
static int inc(int x) { return x + 1; }
static int call0(fptr1 fp) { return fp(0); }
int f1(fptr1 fp, int i) { return fp(i); }
int f2(fptr2 fp, int i) { return fp(i); }
int f3(fptr3 fp, int i) { return fp(i); }
int f4(fptr4 fp, int i) { return fp[i](i); }
int f5(fptr5 fp, fptr1 i) { return fp(i); }
int f8(int ([4]), int);

int main(void) {
  assert(f1(id, 7) == 7);
  assert(f2(inc, 41) == 42);
  assert(f3(inc, 1) == 2);
  return 0;
}
