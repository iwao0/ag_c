// extern int g + inline 関数の受理確認
// 期待: exit=7
extern int g;
inline int add(int a, int b) { return a + b; }
int main(void) { return add(3, 4); }
