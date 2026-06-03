// for ループ + 複数文字変数の累算
// 期待: exit=6 (1+2+3)
main() {
    count = 0;
    for (i = 1; i <= 3; i = i + 1) count = count + i;
    return count;
}
