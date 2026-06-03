// スカラから union への cast (拡張)
// 期待: exit=7
int main(void) {
    union U { int x; char y; };
    return ((union U)7).x;
}
