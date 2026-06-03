// _Alignof(int (*)[3]) = 8
// 期待: exit=8
int main(void) { return _Alignof(int (*)[3]); }
