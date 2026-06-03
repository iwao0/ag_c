// `(char[10]){"hi"}` の複合リテラル
// 'h'+'i' = 104+105 = 209
// 期待: exit=209
int main(void) {
    char *s = (char[10]){"hi"};
    return s[0] + s[1];
}
