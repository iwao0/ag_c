// カンマ式内で複数代入
// x=2, x=6, y=x+10=16
// 期待: exit=16
int main(void) {
    int x = 1;
    int y = (x = x + 1, x = x * 3, x + 10);
    return y;
}
