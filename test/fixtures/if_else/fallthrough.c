// else が無い場合、if 側で return すれば後続には来ない
// 期待: exit=42
main() {
    if (1) return 42;
    return 0;
}
