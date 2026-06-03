// _Alignof(int[10]) は ag_c では 40 (要素サイズ * 要素数を返す実装)
// 期待: exit=40
int main(void) { return _Alignof(int[10]); }
