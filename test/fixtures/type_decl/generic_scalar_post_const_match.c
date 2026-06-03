// 整数定数式は const int としてマッチ (ag_c 実装)
// 期待: exit=2
int main(void) {
    return _Generic(1, int const: 2, default: 3);
}
