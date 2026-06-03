// INT_MAX++ → 負数 (実装定義だが ag_c は wrap)
// 期待: exit=1
int main(void) {
    int x = 2147483647;
    x++;
    return x < 0;
}
