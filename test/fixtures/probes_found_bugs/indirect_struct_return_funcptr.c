// Indirect function pointer call returning an aggregate through a hidden return area.
// Expected: exit=0

typedef struct {
    int a;
    int b;
    int c;
    int d;
    int e;
} big_t;

static big_t make_big(int x) {
    big_t v = {x, x + 1, x + 2, x + 3, x + 4};
    return v;
}

int main(void) {
    big_t (*fp)(int) = make_big;
    big_t v = fp(3);
    if (v.a != 3) return 1;
    if (v.b != 4) return 2;
    if (v.c != 5) return 3;
    if (v.d != 6) return 4;
    if (v.e != 7) return 5;
    return 0;
}
