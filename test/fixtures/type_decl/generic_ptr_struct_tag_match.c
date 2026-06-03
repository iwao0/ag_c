// _Generic で struct S* マッチ (struct T* と区別)
// 期待: exit=2
int main(void) {
    struct S { int x; };
    struct T { int x; };
    struct S s = {1};
    struct S *ps = &s;
    return _Generic(ps, struct T*: 1, struct S*: 2, default: 3);
}
