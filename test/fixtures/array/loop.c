// 配列を for ループで書き込み・読み出し
// arr[i] = i+1 → 1+2+...+10 = 55
// 期待: exit=55
int main(void) {
    int arr[10];
    int i;
    for (i = 0; i < 10; i = i + 1) arr[i] = i + 1;
    int sum = 0;
    for (i = 0; i < 10; i = i + 1) sum = sum + arr[i];
    return sum;
}
