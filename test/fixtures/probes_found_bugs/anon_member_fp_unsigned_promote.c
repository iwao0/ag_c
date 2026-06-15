// 匿名 struct/union (C11 6.7.2.1p13) のメンバを外側 tag へ昇格する際、fp_kind /
// is_bool / is_unsigned / outer_stride が伝播されず、float/double メンバが整数として
// load/store され、unsigned メンバが符号付き比較になっていた。
// 例: `struct { union { int n; float f; }; }` の `s.f` が整数扱いで値が化けた。
// struct_layout の昇格処理でこれらの属性も setter で伝播するよう修正。
struct Mixed {
  union { int n; float f; double d; };  // 匿名 union
  struct { unsigned u; short sh; };     // 匿名 struct
  int tag;
};
int main(void) {
  struct Mixed s;
  int r = 0;

  // 匿名 union の float/double メンバ (fp_kind 伝播)
  s.f = 2.5f;
  if (s.f != 2.5f) r |= 1;
  s.d = 123.625;
  if (s.d != 123.625) r |= 2;
  // 型 punning: int で書いて float で読む (2.0f のビットパターン)
  s.n = 0x40000000;
  if (s.f != 2.0f) r |= 4;

  // 匿名 struct の unsigned メンバ (is_unsigned 伝播 → 符号なし比較)
  s.u = 0xFFFFFFFFu;
  if (s.u != 0xFFFFFFFFu) r |= 8;
  if (s.u <= 100) r |= 16;          // unsigned: 0xFFFFFFFF >u 100
  if (!(s.u > 0x7FFFFFFFu)) r |= 32;

  // 匿名 struct の short メンバと末尾 tag のオフセット
  s.sh = -300;
  if (s.sh != -300) r |= 64;
  s.tag = 77;
  if (s.tag != 77) r |= 128;

  return r == 0 ? 42 : r;
}
