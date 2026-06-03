// flexible array member の宣言が受理されること (char 版)
// 期待: exit=0
int main(void) {
    struct F { int n; char buf[]; };
    return 0;
}
