// sizeof(_Complex float) = 8 (実部+虚部各 4)
// 期待: exit=8
int main(void) { return sizeof(_Complex float); }
