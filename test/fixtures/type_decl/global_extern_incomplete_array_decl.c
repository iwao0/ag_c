// extern による不完全配列宣言 (パース確認)
// 期待: exit=7
extern int a[];
int main(void) { return 7; }
