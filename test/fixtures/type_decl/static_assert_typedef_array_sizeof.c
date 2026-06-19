// typedef 配列型 sizeof を _Static_assert
// 期待: exit=0
typedef int A3[3];
_Static_assert(sizeof(A3) == 12, "ok");
int main(void) { return 0; }
