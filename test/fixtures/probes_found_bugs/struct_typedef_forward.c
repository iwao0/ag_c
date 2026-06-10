// 構造体 forward declaration
struct Node;
typedef struct Node Node;
struct Node { int v; Node *next; };
int main(void) {
  Node b = {2, 0};
  Node a = {1, &b};
  return a.v + a.next->v;
}
// 期待: 3
