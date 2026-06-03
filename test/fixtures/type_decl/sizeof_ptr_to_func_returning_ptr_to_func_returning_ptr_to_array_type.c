// 上記をさらにネスト = 8
// 期待: exit=8
int main(void) { return sizeof(int (*(*(*)(void))(int))[3]); }
