// 短絡評価: && の左が偽なら右は評価されない
// 期待: exit=0 (b は変更されない)
int main(void) {
    int a = 0;
    int b = 0;
    if (a && (b = 1)) b = 2;
    return b;
}
