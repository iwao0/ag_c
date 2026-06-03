// unsigned long 戻り値関数
// 期待: exit=42
unsigned long foo(int x) { return (unsigned long)x; }
int main(void) { return (int)foo(42); }
