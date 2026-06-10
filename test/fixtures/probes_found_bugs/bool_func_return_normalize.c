// _Bool 戻り値関数で `return 200;` を 0/1 に正規化
// 修正前: parser の return 文は戻り値型 _Bool を考慮せず、return 式をそのまま
// 返していた。caller が `flag * 7` のように整数演算に使うと 200*7=1400 が
// 返り、(int) 経由で見ると 130 等の garbage 化していた。
//
// parse_return で current_func_ret_token_kind == TK_BOOL の場合に
// `lhs != 0` を被せて 0/1 に正規化する。
_Bool always_big(int x) { (void)x; return 200; }
_Bool is_pos(int x) { return x > 0; }
int main(void) {
  int r = 0;
  if (is_pos(5)) r += 10;       // 10
  if (is_pos(-3)) r += 100;
  r += always_big(0) * 7;        // 1 * 7
  return r; // 17
}
// 期待: 17
