// 負数の除算は 0 方向に truncate (-7/2 == -3、-7%2 == -1)
// 期待: exit=2
int main(void) {
    int x = -7;
    int y = 2;
    return (x / y == -3) + (x % y == -1);
}
