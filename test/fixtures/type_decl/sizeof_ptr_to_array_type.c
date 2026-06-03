// sizeof(int (*)[3]) = 8 (ポインタ)
// 期待: exit=8
int main(void) { return sizeof(int (*)[3]); }
