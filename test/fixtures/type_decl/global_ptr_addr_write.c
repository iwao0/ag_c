// グローバル int* 経由で g に書き込み
// 期待: exit=55
int g = 0;
int *gp = &g;
int main(void) {
    *gp = 55;
    return g;
}
