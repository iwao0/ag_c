// _Alignof((int[3])) (ag_c 拡張形)。配列のアラインメント = 要素 int のアラインメント = 4。
// 期待: exit=4
int main(void) { return _Alignof((int[3])); }
