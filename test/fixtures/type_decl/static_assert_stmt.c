// 関数内 _Static_assert
// 期待: exit=7
int main(void) {
    _Static_assert(1, "ok");
    return 7;
}
