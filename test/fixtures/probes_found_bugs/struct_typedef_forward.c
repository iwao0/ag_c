// 構造体 forward declaration
#include <assert.h>
struct Node;
typedef struct Node Node;
struct Node { int v; Node *next; };
int main(void) {
  Node b = {2, 0};
  Node a = {1, &b};
  assert(a.v == 1); assert(a.next->v == 2); return 0;
}
// 期待: 3
