// 同型 struct 同士の cast (no-op)
// 期待: exit=7
int main(void) {
    struct S { int x; };
    struct S s = (struct S)(struct S){7};
    return s.x;
}
