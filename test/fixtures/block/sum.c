// ブロック式の値は破棄され、その後の return が有効
// 期待: exit=6
int main(void) {
    int a = 1;
    int b = 2;
    int c = 3;
    { a + b + c; }
    return a + b + c;
}
