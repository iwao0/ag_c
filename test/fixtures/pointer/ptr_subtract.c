// ポインタ - ポインタ: 要素数を返す (C11 6.5.6p9)
// int *p = &arr[4], *q = &arr[1] → p - q == 3 (バイト差ではなく要素差)
// 期待: exit=3
int main(void) {
    int arr[5] = {1, 2, 3, 4, 5};
    int *p = &arr[4];
    int *q = &arr[1];
    return (int)(p - q);
}
