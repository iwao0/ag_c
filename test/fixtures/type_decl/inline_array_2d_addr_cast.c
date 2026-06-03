// 2 次元配列に対する `&arr` のキャスト経由アクセス。
// arr[2][3] = 77 を書き、((int*)&arr)[2*4 + 3] = ((int*)&arr)[11] で読み戻す。
// 期待: exit=77
int main(void) {
    int arr[3][4];
    arr[2][3] = 77;
    int *p = (int*)&arr;
    return p[2 * 4 + 3];
}
