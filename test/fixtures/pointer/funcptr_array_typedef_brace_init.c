// typedef した関数ポインタ型の配列を波括弧初期化
// 期待: exit=17 (3+4 + 2*5)
typedef int (*binop_t)(int, int);
int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }
int main(void) {
    binop_t ops[2] = { add, mul };
    return ops[0](3, 4) + ops[1](2, 5);
}
