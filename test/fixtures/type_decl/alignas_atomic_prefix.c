// _Alignas / _Atomic 前置修飾
// 期待: exit=7 (3+4)
int main(void) {
    _Alignas(16) int x = 3;
    _Atomic int y = 4;
    return x + y;
}
