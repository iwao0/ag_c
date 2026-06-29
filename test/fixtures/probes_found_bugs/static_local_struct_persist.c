// `static struct S a = {...};` の struct/union 局所変数が呼び出しを跨いで永続する。
// 旧実装は static dispatch が tag_kind==TK_EOF のスカラ/配列のみグローバルへ lowering し、
// struct/union は通常の auto 局所として扱われ毎回初期化子で再初期化されていた
// (永続しない)。さらに stmt.c の tag-keyword 経路が storage class を素通りスキップして
// いたため static フラグ自体が失われていた。両方を直し、struct 初期化子を
// psx_parse_global_brace_init_flat でグローバル struct data へ落とすようにした。
#include <assert.h>
struct Acc { int sum; int count; };
struct V { double x; double y; };
union U { int i; char c[4]; };
struct P { int a, b, c; };

int next_acc(void){
  static struct Acc a = {100, 0};   // 永続: 110/1, 120/2, 130/3
  a.sum += 10;
  a.count++;
  return a.sum + a.count;
}
int accum_double(void){
  static struct V v = {1.5, 2.5};   // 永続 + double メンバ
  v.x += 1.0;
  v.y += 1.0;
  return (int)(v.x + v.y);
}
int bump_union(void){
  static union U u = {0x41424344};  // 永続 + union
  u.i++;
  return u.c[0];                     // little-endian 下位バイト
}
int zero_init(void){
  static struct P p;                 // 初期化子なし → ゼロ + 永続
  p.a += 3; p.b += 7;
  return p.a + p.b;
}
int designated(void){
  static struct P q = {.b = 5};      // designated init + 永続
  q.a++;
  return q.a + q.b;
}
int anon_struct_persist(void){
  static struct { int n; int m; } s = {3, 4};
  s.n += 1;
  s.m += 2;
  return s.n * 10 + s.m;
}
int anon_union_persist(int use_b){
  static union { int a; int b; } u = {5};
  if (use_b) u.b += 2; else u.a += 1;
  return u.a;
}
int anon_array_persist(void){
  static struct { int n; int m; } a[2] = {{1, 2}, {3, 4}};
  a[0].n += 3;
  a[1].m += 5;
  return a[0].n + a[0].m + a[1].n + a[1].m;
}

int main(void){
  int r = 0;

  if (next_acc() != 111) r |= 1;
  if (next_acc() != 122) r |= 2;
  if (next_acc() != 133) r |= 4;     // 永続して累積

  if (accum_double() != 6) r |= 8;   // 1回目: x=2.5, y=3.5 -> 6
  if (accum_double() != 8) r |= 16;  // 2回目: x=3.5, y=4.5 -> 8 (永続)

  if (bump_union() != 0x45) r |= 32; // 0x44->0x45 = 'E'
  if (bump_union() != 0x46) r |= 64; // 永続

  zero_init();                        // 10
  if (zero_init() != 20) r |= 128;   // 永続して 20

  designated();                       // a=1,b=5 -> 6
  if (designated() != 7) r |= 256;   // 永続: a=2,b=5 -> 7

  if (anon_struct_persist() != 46) r |= 512;
  if (anon_struct_persist() != 58) r |= 1024;

  if (anon_union_persist(0) != 6) r |= 2048;
  if (anon_union_persist(1) != 8) r |= 4096;

  if (anon_array_persist() != 18) r |= 8192;
  if (anon_array_persist() != 26) r |= 16384;

  return 0;
}
