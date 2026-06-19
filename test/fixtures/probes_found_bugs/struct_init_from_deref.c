// 構造体の swap (ポインタ経由)
#include <assert.h>
struct P { int x; int y; };
void swap(struct P *a, struct P *b) {
  struct P t = *a;
  *a = *b;
  *b = t;
}
int main(void) {
  struct P p = {3, 4}, q = {7, 9};
  swap(&p, &q);
  assert(p.x == 7); assert(p.y == 9); assert(q.x == 3); assert(q.y == 4); return 0;
}
// 期待: 7+9+3+4 = 23
