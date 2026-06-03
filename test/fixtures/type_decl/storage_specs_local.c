// ローカル変数に static/register/auto + restrict ポインタ
// 8+2+1+1 = 12
// 期待: exit=12
int main(void) {
    static int x = 8;
    register int r = 2;
    auto int a = 1;
    int *restrict p = 0;
    return x + r + a + (p == 0);
}
