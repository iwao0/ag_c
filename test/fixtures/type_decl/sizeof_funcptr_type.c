// sizeof(関数ポインタ) = 8
// 期待: exit=8
int main(void) { return sizeof(int (*)(int)); }
