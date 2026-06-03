// _Alignof((int[3])) は ag_c では 12
// 期待: exit=12
int main(void) { return _Alignof((int[3])); }
