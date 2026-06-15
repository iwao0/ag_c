// サイズが 1/2/4/8 でない struct/union (3/5/6/7 バイト) を値で渡す・返すと、
// 先頭メンバ幅のスカラとして 1 レジスタロードされ、先頭メンバしか復元できず
// 残りが化けていた (`struct{char a; short b; uchar c;}` の 6B が 1B 扱い)。
// >8B と同様にアドレス渡し / ret_area(x8) 経由の間接 ABI に回すことで修正。
struct Small { char a; short b; unsigned char c; };   // 6 bytes
struct P3 { char x, y, z; };                            // 3 bytes
struct S7 { char d[7]; };                               // 7 bytes

struct Small twice(struct Small s){ s.a *= 2; s.b *= 2; s.c *= 2; return s; }
int reada(struct Small s){ return s.a; }
int readb(struct Small s){ return s.b; }
struct P3 combine(struct P3 a, struct P3 b){ struct P3 r = {a.x+b.x, a.y+b.y, a.z+b.z}; return r; }
struct S7 mk7(void){ struct S7 s = {{1,2,3,4,5,6,7}}; return s; }

int main(void) {
  int t = 0;

  // 値渡しで全メンバ読める
  struct Small s = {10, 1000, 100};
  t += (reada(s) == 10);
  t += (readb(s) == 1000);

  // 値渡し + 修正して値返し
  struct Small r = twice(s);
  t += (r.a == 20) + (r.b == 2000) + (r.c == 200);
  t += (s.a == 10);  // 元は不変

  // 3 バイト struct: 値渡し合成 + 値返し
  struct P3 c = combine((struct P3){1,2,3}, (struct P3){4,5,6}); // {5,7,9}
  t += (c.x == 5) + (c.y == 7) + (c.z == 9);

  // 7 バイト struct 値返し
  struct S7 seven = mk7();
  int sum7 = 0;
  for (int i = 0; i < 7; i++) sum7 += seven.d[i];
  t += (sum7 == 28);

  return t + 32;  // 10 checks true -> 10+32 = 42
}
