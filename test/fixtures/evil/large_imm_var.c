// 大きな即値を変数経由で剰余
// 1000000 % 256 = 64
// 期待: exit=64
int main(void) {
    int x = 1000000;
    return x % 256;
}
