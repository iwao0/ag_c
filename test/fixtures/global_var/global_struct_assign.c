// グローバル struct (初期化子なし) にメンバ単位で書き込み・読み出し
// 期待: exit=42 (10 + 32)
struct P { int x; int y; };
struct P p;
int main(void) {
    p.x = 10;
    p.y = 32;
    return p.x + p.y;
}
