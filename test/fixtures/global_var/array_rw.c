// グローバル配列への書き込みと読み出し
// 期待: exit=20 (g[1])
int g[3];
int main(void) {
    g[0] = 10;
    g[1] = 20;
    g[2] = 30;
    return g[1];
}
