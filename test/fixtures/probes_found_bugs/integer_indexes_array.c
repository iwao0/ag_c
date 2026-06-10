// integer-indexes-array 形式 `3[a]` (C11 6.5.2.1p1: a[b] ≡ b[a])
// 修正前: build_subscript_deref が左辺をベース・右辺をインデックスと決め打ちし、
// `3[a]` のとき左 (ND_NUM=3) を base アドレスとして subscript_base_address_of に
// 渡してしまい、生成 asm が 3 を絶対アドレスとして deref → segfault。
int main(void) {
  int a[5] = {1, 2, 3, 4, 5};
  return a[3] + 3[a]; // 4 + 4 = 8
}
// 期待: 8
