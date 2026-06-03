// 配列サイズ推定: 末尾カンマあり (要素数に含めない)
// 期待: exit=15 (1+2+3+4+5)
int main(void) {
    int a[] = {1, 2, 3, 4, 5,};
    int sum = 0;
    for (int i = 0; i < 5; i++) sum += a[i];
    return sum;
}
