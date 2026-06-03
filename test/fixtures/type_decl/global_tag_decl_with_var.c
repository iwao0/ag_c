// グローバル: タグ定義と同時にポインタ変数宣言
// 期待: exit=7
struct S { int x; } *gp;
int main(void) { return 7; }
