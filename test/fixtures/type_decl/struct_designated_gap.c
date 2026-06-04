// 構造体の指定初期化子で歯抜けにすると、未指定メンバは 0 で初期化 (C11 6.7.9p21)
// struct P p = {.y=30, .x=10} → p.z = 0、合計 = 40
// 期待: exit=40
int main(void) {
    struct P { int x, y, z; };
    struct P p = {.y = 30, .x = 10};
    return p.x + p.y + p.z;
}
