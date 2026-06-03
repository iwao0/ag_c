// 配列サイズに定数式を使えること
// `int arr[1+2]` で要素数 3 として扱われる。
// 期待: exit=3
int main(void) {
    int arr[1 + 2];
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    return arr[2];
}
