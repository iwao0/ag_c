// (int const const) で重複修飾子をキャスト
// 期待: exit=21
int main(void) { return (int const const)21; }
