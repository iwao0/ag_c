// signed char に 200 を代入 → -56 として解釈
// 期待: exit=1
int main(void) {
    signed char c = 200;
    return (c < 0);
}
