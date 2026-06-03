// ポインタ経由の代入 (*p = value で元変数が更新される)
// 期待: exit=10
int main(void) { int x = 5; int *p = &x; *p = 10; return x; }
