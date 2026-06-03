// ポインタ to 関数 returning ポインタ to 関数 = 8
// 期待: exit=8
int main(void) { return sizeof(int (*(*)(void))(int)); }
