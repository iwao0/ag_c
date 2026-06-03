// _Generic で union 型 assoc
// 期待: exit=1
int main(void) {
    union U { int x; };
    return _Generic((union U){.x = 1}, union U: 1, default: 2);
}
