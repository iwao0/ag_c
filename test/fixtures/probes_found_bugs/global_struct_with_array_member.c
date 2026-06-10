// グローバル struct の配列メンバ初期化 (`struct V { int values[3]; int total; }`)
// 修正前: gen_global_vars (arm64_apple.c:295) の struct ブランチが各メンバを
// 「1 init_values[] 要素」として扱い、配列メンバを展開せずに 1 値だけ書いて
// いたため `{ {10, 20, 30}, 60 }` で values[0]=10 + (padding) + total=20 という
// asm を生成し、values[1]/values[2]/total=60 が捨てられていた。
//
// 修正: 配列メンバ (alen>0) を検出したら ts*alen 全体に対して連続 alen 値を
// 出力する分岐を追加。struct_layout は配列メンバの type_size を「要素サイズ」
// (struct_layout.c:247) で登録するため、全体サイズは ts*alen となる。
// init_values のインデックスもメンバ index と独立した val_idx で進める。
struct V { int values[3]; int total; };
struct V g = {{10, 20, 30}, 60};
int main(void) {
  return g.values[0] + g.values[1] + g.values[2] + g.total; // 10+20+30+60 = 120
}
// 期待: 120
