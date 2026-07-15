/* `_Generic` で long double を double と区別する (C11 6.2.5: 同表現でも別型)。
 * ag_c は long double を double に lowering し fp_kind=DOUBLE になるため、以前は
 * long double 制御式が `double:` / `default:` に当たっていた。long long / plain char と
 * canonical typeのfp_kindをinfer_generic_control_typeが読み、
 * `long double:` 関連型と一致させる。
 * 値は常に double と同一 (Apple ARM64 で long double==double) なので _Generic の選択のみ
 * が観測差。local/global/param と typedef 経由の宣言へも fp_kind を伝播する。 */
#include <assert.h>

#define KIND(x) _Generic((x), long double: 3, double: 2, float: 1, default: 0)
#define CAST_KIND(T, x) KIND((T)(x))

typedef long double LD;
typedef LD LD2;

long double gld = 1.5L;
double gd = 2.5;
LD gtd = 3.5L;
LD2 gtd2 = 4.5L;

static int classify_ld(long double x) { return KIND(x); }
static int classify_double(double x) { return KIND(x); }
static int classify_typedef_param(LD x) { return KIND(x); }
static int classify_typedef_chain_param(LD2 x) { return _Generic((x), LD: 7, double: 2, default: 0); }

int main(void) {
  long double ld = 1.5L;
  double d = 2.5;
  float f = 0.5f;
  LD td = 3.5L;
  LD2 td2 = 4.5L;

  /* ローカル変数 */
  assert(KIND(ld) == 3);
  assert(KIND(d) == 2);
  assert(KIND(f) == 1);
  assert(KIND(td) == 3);
  assert(KIND(td2) == 3);
  assert(_Generic((td), LD: 7, double: 2, default: 0) == 7);

  /* グローバル変数・仮引数 */
  assert(KIND(gld) == 3);
  assert(KIND(gd) == 2);
  assert(KIND(gtd) == 3);
  assert(KIND(gtd2) == 3);
  assert(classify_ld(1.0L) == 3);
  assert(classify_double(1.0) == 2);
  assert(classify_typedef_param(2.0L) == 3);
  assert(classify_typedef_chain_param(3.0L) == 7);

  /* cast (制御式に直接置く形と、マクロ経由で二重括弧になる形)。 */
  assert(_Generic((long double)1.0, long double: 3, double: 2, default: 0) == 3);
  assert(_Generic((double)1.0, long double: 3, double: 2, default: 0) == 2);
  assert(CAST_KIND(long double, 1.0) == 3);
  assert(CAST_KIND(double, 1.0L) == 2);
  assert(CAST_KIND(LD, 1.0) == 3);

  /* long double の値は double と同一に評価される (lowering)。区別は型のみ。 */
  assert((double)ld == 1.5);

  /* long double 関連型が無いとき long double 制御式は default に落ちる (C11 準拠) */
  assert(_Generic((long double)1.0, double: 2, default: 9) == 9);
  assert(_Generic(ld, double: 2, default: 9) == 9);

  return 0;
}
