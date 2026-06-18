// _Alignof が struct/union/配列に対して「サイズ」を返していた (sizeof と同じ実装)。
// 基本型は size==align で偶然合うが、`struct{int,int}` (size 8/align 4) や
// `int[10]` (size 40/align 4)、`_Alignas(16) int` を含む struct で誤っていた。
// struct のアラインメント (agg_align) を tag テーブルへ保存し、_Alignof は配列で
// 要素数を掛けず、struct はその align を返すよう修正。
#include <assert.h>
struct P { int x, y; };               // align 4, size 8
struct Q { char a; double b; };       // align 8, size 16
struct A { char c; _Alignas(16) int x; }; // align 16, size 32
struct Small { char a, b; };          // align 1, size 2
union U { char c; long l; };          // align 8

int main(void) {
  assert(_Alignof(struct P) == 4);
  assert(_Alignof(struct Q) == 8);
  assert(_Alignof(struct A) == 16);
  assert(_Alignof(struct Small) == 1);
  assert(_Alignof(union U) == 8);
  assert(_Alignof(int[10]) == 4);          // 配列 = 要素アラインメント
  assert(_Alignof(struct Q[3]) == 8);

  // 基本型は従来通り
  assert(_Alignof(char) == 1);
  assert(_Alignof(int) == 4);
  assert(_Alignof(double) == 8);
  assert(_Alignof(int*) == 8);

  // sizeof は変わらない
  assert(sizeof(struct P) == 8);
  assert(sizeof(struct A) == 32);
  return 0;
}
