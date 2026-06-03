// _Atomic(int) 型指定子
// 期待: exit=5
int main(void) {
    _Atomic(int) z = 5;
    return z;
}
