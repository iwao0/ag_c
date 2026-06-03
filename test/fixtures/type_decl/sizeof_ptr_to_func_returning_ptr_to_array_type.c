// ポインタ to 関数 returning ポインタ to 配列 = 8 (ポインタ)
// 期待: exit=8
int main(void) { return sizeof(int (*(*)(void))[3]); }
