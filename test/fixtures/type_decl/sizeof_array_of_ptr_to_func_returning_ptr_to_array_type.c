// 上記の配列 [2] = 16
// 期待: exit=16
int main(void) { return sizeof(int (*(*[2])(void))[3]); }
