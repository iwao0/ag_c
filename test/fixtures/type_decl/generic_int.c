// _Generic で int 選択
// 期待: exit=11
int main(void) {
    return _Generic(1, int: 11, default: 22);
}
