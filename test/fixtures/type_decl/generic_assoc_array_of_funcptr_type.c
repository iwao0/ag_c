// 配列型 assoc (関数ポインタ配列) はマッチしない → default
// 期待: exit=2
int main(void) {
    return _Generic((int (*)(int))0, int (*[3])(int): 1, default: 2);
}
