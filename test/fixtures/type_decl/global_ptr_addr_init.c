// グローバル int* を &g で初期化
// 期待: exit=99
int g = 99;
int *gp = &g;
int main(void) { return *gp; }
