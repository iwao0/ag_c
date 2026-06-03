// 指定初期化子 [N]= による要素割り当て (C99)
// arr[0]=1, arr[2]=7, 他は 0
// 期待: exit=8 (arr[0]+arr[2])
int main(void) {
    int arr[4] = { [2] = 7, [0] = 1 };
    return arr[0] + arr[2];
}
