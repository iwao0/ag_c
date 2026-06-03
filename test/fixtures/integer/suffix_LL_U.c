// 整数リテラルの LL / U サフィックス
// 期待: exit=6 (1+5)
int main(void) {
    long long v = 1LL;
    unsigned int u = 5U;
    return (int)(v + u);
}
