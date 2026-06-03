// sizeof(_Complex double) = 16 (実部+虚部各 8)
// 期待: exit=16
int main(void) { return sizeof(_Complex double); }
