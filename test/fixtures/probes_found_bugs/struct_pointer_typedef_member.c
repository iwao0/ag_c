// 続き58: pointer typedef を struct メンバに使ったときの幅 (miscompile 修正)。
//
// 修正前: `typedef int (*FnPtr)(int); struct H { FnPtr fn; };` で `FnPtr fn` メンバが
// elem_size=4 (関数戻り型 int のサイズ) で扱われ、`h.fn = sqr` の store が `str w20, [x19]`
// (32bit) に潰されていた。後段 `h.fn(3)` 呼び出しでアドレス上位 32bit が失われ
// SIGSEGV。
//
// 修正: struct_layout の typedef 解決で psx_typedef_info_t::is_pointer を見て、ポインタ
// typedef なら elem_size=8 に強制する。
#include <assert.h>

typedef int (*FnPtr)(int);
typedef int *IntPtr;
typedef const char *CStr;
typedef int (*BinOp)(int, int);

struct H { FnPtr fn; const char *name; };
struct IP { IntPtr p; int count; };
struct Named { CStr label; int val; };
struct Calc { BinOp op; int lhs; int rhs; };

int sqr(int x) { return x * x; }
int cube(int x) { return x * x * x; }
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

int main(void) {
    /* (a) 関数ポインタ typedef */
    struct H h = { sqr, "square" };
    assert(h.fn(3) == 9);
    assert(h.fn(4) == 16);
    h.fn = cube;
    assert(h.fn(3) == 27);

    /* (b) 配列のポインタ要素として */
    struct H hs[2] = { { sqr, "sq" }, { cube, "cu" } };
    assert(hs[0].fn(3) == 9);
    assert(hs[1].fn(3) == 27);

    /* (c) int ポインタ typedef */
    int data[3] = { 100, 200, 300 };
    struct IP ip = { data, 3 };
    int sum = 0;
    for (int i = 0; i < ip.count; i++) sum += ip.p[i];
    assert(sum == 600);

    /* (d) const char ポインタ typedef */
    struct Named n = { "hello", 42 };
    assert(n.label[0] == 'h');
    assert(n.label[4] == 'o');
    assert(n.val == 42);

    /* (e) 2 引数関数ポインタ typedef */
    struct Calc c1 = { add, 10, 3 };
    struct Calc c2 = { sub, 10, 3 };
    assert(c1.op(c1.lhs, c1.rhs) == 13);
    assert(c2.op(c2.lhs, c2.rhs) == 7);

    return 0;
}
