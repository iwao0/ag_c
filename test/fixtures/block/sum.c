// ブロック式の値は破棄され、その後の return が有効
// 期待: exit=6
main() {
    a = 1;
    b = 2;
    c = 3;
    { a + b + c; }
    return a + b + c;
}
