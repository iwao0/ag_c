// 構造体の swap (ポインタ経由)
struct P { int x; int y; };
void swap(struct P *a, struct P *b) {
  struct P t = *a;
  *a = *b;
  *b = t;
}
int main(void) {
  struct P p = {3, 4}, q = {7, 9};
  swap(&p, &q);
  return p.x + p.y + q.x + q.y;
}
// 期待: 7+9+3+4 = 23
