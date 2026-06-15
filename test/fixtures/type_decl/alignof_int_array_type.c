// _Alignof(int[10]) は配列のアラインメント = 要素 int のアラインメント = 4 (C11)。
// 期待: exit=4
int main(void) { return _Alignof(int[10]); }
