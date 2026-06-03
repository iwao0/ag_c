// グローバル変数を同名ローカルでシャドウ
// 期待: exit=10
int x = 50;
int main(void) {
    int x = 10;
    return x;
}
