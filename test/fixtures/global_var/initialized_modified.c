// 初期化済みグローバル変数を関数内で更新
// 期待: exit=42 (10+32)
int g = 10;
int main(void) {
    g = g + 32;
    return g;
}
