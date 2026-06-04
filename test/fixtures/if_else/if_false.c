// if 条件が偽のとき else 側の return が実行される
// 期待: exit=0
int main(void) {
    int a = 3;
    if (a == 5) return a;
    else return 0;
}
