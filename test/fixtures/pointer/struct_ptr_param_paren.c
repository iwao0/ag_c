// `struct S (*p)` 形式の冗長な括弧付き宣言
// 期待: exit=42
struct S { int x; };
int get(struct S (*p)) { return p->x; }
int main(void) {
    struct S s = {42};
    return get(&s);
}
