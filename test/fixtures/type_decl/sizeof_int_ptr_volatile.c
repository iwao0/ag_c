// sizeof(int * volatile) = 8
// 期待: exit=8
int main(void) { return sizeof(int * volatile); }
