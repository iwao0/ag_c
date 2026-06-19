// struct/union ポインタの加減算 (`&s[i]-&s[j]`, `sp+n`, `sp-n`, `n+sp`) が
// 要素サイズでスケーリング/除算されず byte 単位になっていた。原因は
// struct タグポインタが is_pointer ではなく is_tag_pointer で表現され、add() の
// ポインタ算術判定 (psx_node_is_pointer) が偽を返していたこと。
// node_is_ptr_for_arith でタグポインタもポインタとして扱う。
#include <assert.h>
struct P { int x, y, z; };   // 12 bytes
union U { int i; char c[4]; }; // 4 bytes

int main(void) {
  int t = 0;

  struct P pa[6];
  for (int i = 0; i < 6; i++) { pa[i].x = i * 10; pa[i].y = i; pa[i].z = -i; }

  // ポインタ - ポインタ: 要素数 (byte 差 / sizeof) になる
  t += (&pa[5] - &pa[1]);       // 4

  // ポインタ + 整数 / 整数 + ポインタ
  struct P *p = pa;
  t += (p + 3)->x;              // pa[3].x = 30
  t += (2 + p)->x;              // pa[2].x = 20

  // ポインタ - 整数
  struct P *q = &pa[5];
  t += (q - 2)->x;              // pa[3].x = 30

  // ループでの pp++ と pp < pa + N
  int sx = 0;
  for (struct P *pp = pa; pp < pa + 6; pp++) sx += pp->y; // 0+1+2+3+4+5 = 15
  t += sx;

  // union ポインタも同様
  union U ua[4];
  for (int i = 0; i < 4; i++) ua[i].i = i;
  t += (&ua[3] - &ua[0]);      // 3

  assert(t == 102); return 0;   // 4+30+20+30+15+3 = 102 ; 102-60 = 42
}
