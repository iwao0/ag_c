// 深いネスト型の assoc (パース確認、マッチしない)
// 期待: exit=2
int main(void) {
    return _Generic((int (*(*)(void))[3])0, int (*(*)(void))[3]: 1, default: 2);
}
