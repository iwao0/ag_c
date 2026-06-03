// 構造体配列メンバの合計
// 10+20+30+40 = 100
// 期待: exit=100
int main(void) {
    struct S { int arr[4]; int n; };
    struct S s;
    s.n = 4;
    s.arr[0] = 10;
    s.arr[1] = 20;
    s.arr[2] = 30;
    s.arr[3] = 40;
    int sum = 0;
    int i = 0;
    for (i = 0; i < s.n; i = i + 1) sum = sum + s.arr[i];
    return sum % 256;
}
