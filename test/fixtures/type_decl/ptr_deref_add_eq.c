// ポインタ経由の複合代入 *p += 2
// 期待: exit=7
int main(void) {
    int x = 5;
    int *p = &x;
    *p += 2;
    return x;
}
