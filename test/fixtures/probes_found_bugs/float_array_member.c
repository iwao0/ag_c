// struct の float/double 配列メンバ `float v[N]` のアクセスと初期化が壊れていた。
// アクセス: build_member_deref_node が fp_kind を base.fp_kind に設定していたが、配列
//   メンバは decay してポインタになるので pointee_fp_kind に入れるべきで、subscript
//   結果が整数 load になり値が化けていた (is_bool と同じ分岐に修正)。
// 初期化: parse_member_initializer の配列要素 store に fp_kind が伝播せず整数 store に
//   なっていた (member_fp_kind を通し fp store にする)。
struct Stats { float values[4]; int count; };
struct DVec { double d[3]; };

int main(void) {
  int t = 0;

  // brace 初期化 + 添字アクセス
  struct Stats s = {{1.5f, 2.5f, 3.5f, 4.5f}, 4};
  t += (s.values[0] == 1.5f);
  t += (s.values[3] == 4.5f);
  t += (s.count == 4);
  float sum = 0;
  for (int i = 0; i < s.count; i++) sum += s.values[i];
  t += (sum == 12.0f);

  // 代入 + アクセス
  struct Stats a;
  for (int i = 0; i < 4; i++) a.values[i] = (float)i * 0.5f;
  t += (a.values[2] == 1.0f);

  // double 配列メンバ
  struct DVec v = {{1.25, 2.5, 3.75}};
  double ds = v.d[0] + v.d[1] + v.d[2];
  t += (ds == 7.5);

  return t + 36;  // 6 checks -> 6+36 = 42
}
