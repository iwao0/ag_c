// UCN を含む識別子 (ñ = U+00F1)
// 期待: exit=7
int main(void) {
    int \u00f1 = 7;
    return \u00f1;
}
