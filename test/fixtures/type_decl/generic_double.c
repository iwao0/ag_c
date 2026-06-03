// _Generic で double 選択
// 期待: exit=33
int main(void) {
    return _Generic(1.0, float: 11, double: 33, default: 22);
}
