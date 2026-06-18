// struct を返す関数の戻り値を直接 `return funccall();` で転送する。
// 旧実装の build_stmt_return は struct 戻り source が ND_LVAR/ND_DEREF/ND_COMMA のみ
// 対応し、ND_FUNCALL を "struct return value is not LVAR/DEREF" で fail させ
// ir_build_module を失敗させていた (`struct A r = make(); return r;` と一旦変数へ
// 入れると動いていた)。間接 struct 戻りの funccall は ret_area の PTR を返すので、
// それを memcpy ソースにするケースを追加して修正。
struct S8  { int a, b; };                 // 8B  (レジスタ返し)
struct S16 { long sum; int count; };      // 16B (間接)
struct S24 { int a, b, c, d, e, f; };     // 24B (間接, x8)

struct S8  mk8(int x){ struct S8 s = {x, x * 2}; return s; }
struct S8  fwd8(int x){ return mk8(x); }                 // return 8B funccall

struct S16 mk16(long s, int c){ struct S16 r = {s, c}; return r; }
struct S16 fwd16(long s, int c){ return mk16(s, c); }    // return 16B funccall

struct S24 mk24(int b){ struct S24 r = {b, b+1, b+2, b+3, b+4, b+5}; return r; }
struct S24 fwd24(int b){ return mk24(b); }               // return 24B funccall

// >8B struct を値で受けて再帰し、`return rec(...)` で戻す
struct S16 accumulate(int *a, int n, struct S16 acc){
  if (n == 0) return acc;
  acc.sum += a[0];
  acc.count++;
  return accumulate(a + 1, n - 1, acc);
}

int main(void) {
  int r = 0;

  struct S8 a = fwd8(5);
  if (a.a != 5 || a.b != 10) r |= 1;

  struct S16 b = fwd16(100, 7);
  if (b.sum != 100 || b.count != 7) r |= 2;

  struct S24 c = fwd24(10);
  if (c.a != 10 || c.f != 15) r |= 4;

  int data[5] = {3, 1, 4, 1, 5};
  struct S16 init = {0, 0};
  struct S16 acc = accumulate(data, 5, init);
  if (acc.sum != 14 || acc.count != 5) r |= 8;

  return r == 0 ? 42 : r;
}
