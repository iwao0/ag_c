// 複数の inline 関数の共存
// add(3,4)=7, mul(2,5)=10 → 17
// 期待: exit=17
inline int add(int a, int b) { return a + b; }
inline int mul(int a, int b) { return a * b; }
int main(void) { return add(3, 4) + mul(2, 5); }
