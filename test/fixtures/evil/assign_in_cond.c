// if 条件内での代入式 (x = 5 → x が真)
// 期待: exit=5
int main(void) {
    int x;
    if (x = 5) {
        return x;
    }
    return 0;
}
