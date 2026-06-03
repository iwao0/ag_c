// コラッツ予想風の再帰カウント
// count(6) = 9 (6→3→10→5→16→8→4→2→1)
// 期待: exit=9
int count(int n) {
    if (n <= 0) return 0;
    if (n == 1) return 1;
    if (n % 2 == 0) return 1 + count(n / 2);
    return 1 + count(n * 3 + 1);
}
int main(void) {
    return count(6);
}
