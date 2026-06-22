/* `_Generic` で long double を double と区別する (C11 6.2.5: 同表現でも別型)。
 * ag_c は long double を double に lowering し fp_kind=DOUBLE になるため、以前は
 * long double 制御式が `double:` / `default:` に当たっていた。long long / plain char と
 * 同じ side-channel ビット (node_mem_t.is_long_double) を宣言時に立て、ノードへ伝播し、
 * infer_generic_control_type が読んで `long double:` 関連型と一致させる。
 * 値は常に double と同一 (Apple ARM64 で long double==double) なので _Generic の選択のみ
 * が観測差。ローカル変数と cast `(long double)` で機能する (long long/plain char と同じ範囲。
 * 仮引数・グローバルは long long 含め _Generic 型区別ビットを伝播しない既存制約のまま)。 */
#include <assert.h>

#define KIND(x) _Generic((x), long double: 3, double: 2, float: 1, default: 0)

int main(void) {
  long double ld = 1.5L;
  double d = 2.5;
  float f = 0.5f;

  /* ローカル変数 */
  assert(KIND(ld) == 3);
  assert(KIND(d) == 2);
  assert(KIND(f) == 1);

  /* cast (制御式に直接置く形。`_Generic((T)expr, ...)` は parse_generic_selection が
   * キャスト型を静的型として採用する。マクロ経由の二重括弧 `((T)expr)` は別経路で未対応)。 */
  assert(_Generic((long double)1.0, long double: 3, double: 2, default: 0) == 3);
  assert(_Generic((double)1.0, long double: 3, double: 2, default: 0) == 2);

  /* long double の値は double と同一に評価される (lowering)。区別は型のみ。 */
  assert((double)ld == 1.5);

  /* long double 関連型が無いとき long double 制御式は default に落ちる (C11 準拠) */
  assert(_Generic((long double)1.0, double: 2, default: 9) == 9);
  assert(_Generic(ld, double: 2, default: 9) == 9);

  return 0;
}
