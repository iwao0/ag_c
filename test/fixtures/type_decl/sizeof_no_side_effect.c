// sizeof のオペランドは評価されない (C11 6.5.3.4p2)
// side_effect() を sizeof の中で書いても n は変わらない
// 期待: exit=1 (sizeof 後の最初の呼出は n=1)
int n;
int side_effect(void) { return ++n; }
int main(void) {
    (void)sizeof(side_effect());
    return side_effect();
}
