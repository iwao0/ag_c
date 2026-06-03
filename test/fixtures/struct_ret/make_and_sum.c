// 小サイズ struct (≤8B) を関数戻り値で返す
// 期待: exit=42 (10+32)
struct Point { int x; int y; };
struct Point make_point(int x, int y) {
    struct Point p = {x, y};
    return p;
}
int main(void) {
    struct Point r = make_point(10, 32);
    return r.x + r.y;
}
