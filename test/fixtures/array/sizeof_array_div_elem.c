// sizeof(arr) / sizeof(arr[0]) で要素数を取得する慣用句
// int a[10] → sizeof(a) = 40、sizeof(a[0]) = 4 → 10
// 期待: exit=10
int main(void) {
    int a[10];
    return (int)(sizeof(a) / sizeof(a[0]));
}
