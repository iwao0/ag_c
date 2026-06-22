/* 欠落していた C11 標準ヘッダ 10 個を同梱: iso646.h / stdalign.h / stdnoreturn.h /
 * uchar.h / inttypes.h / fenv.h / locale.h / wctype.h / wchar.h / tgmath.h。
 * 併せて、これらの対応中に判明した 4 件のコンパイラバグも修正:
 *   - `long double` が _Generic 関連型でパースできない (parse_integer_cast_spec が `long`
 *     だけ食べ `double` を残す)
 *   - `double` 制御式が `long double:` 関連型に誤マッチ (kind=TK_DOUBLE で区別不能)→
 *     is_long_double で区別
 *   - 外部関数のアドレス取得 (`fp = sqrt`) が adrp @PAGE 直参照でリンク失敗→GOT 経由
 *   - `_Generic(...)(args)` 等の bare funcref 呼び出しが間接化しシグネチャを失い fp 戻り値を
 *     x0 で読んでいた→funcref callee を直接呼び出しへ変換しプロトタイプの ABI を適用
 * 関数の実体はシステム libc にリンク。ASCII 内容のみ対応。 */
#include <assert.h>
#include <iso646.h>
#include <stdalign.h>
#include <stdnoreturn.h>
#include <uchar.h>
#include <inttypes.h>
#include <fenv.h>
#include <locale.h>
#include <wctype.h>
#include <wchar.h>
#include <tgmath.h>

noreturn void die(void);   /* stdnoreturn: noreturn -> _Noreturn */

int main(void) {
  /* iso646: 演算子の代替綴り */
  int a = 1, b = 0;
  assert((a and not b) == 1);
  assert((a bitor 4) == 5);

  /* stdalign */
  _Alignas(16) char buf[16];
  assert(alignof(double) == 8 && sizeof(buf) == 16);
  assert(__alignas_is_defined == 1);

  /* uchar: char16_t / char32_t */
  char16_t c16 = u'A';
  char32_t c32 = U'B';
  assert(c16 == 'A' && c32 == 'B');
  assert(sizeof(char16_t) == 2 && sizeof(char32_t) == 4);

  /* inttypes: 書式マクロと imaxabs (libc) */
  imaxdiv_t qr = (imaxdiv_t){7, 1};
  assert(qr.quot == 7 && qr.rem == 1);
  assert(imaxabs((intmax_t)-9) == 9);

  /* fenv: 例外フラグ操作 (libc) */
  feclearexcept(FE_ALL_EXCEPT);
  volatile double x = 1.0, y = 3.0, z = x / y;
  (void)z;
  assert(fetestexcept(FE_INEXACT) != 0);

  /* locale: localeconv (libc) */
  setlocale(LC_ALL, "C");
  struct lconv *lc = localeconv();
  assert(lc->decimal_point[0] == '.');

  /* wctype: ワイド文字分類 (libc) */
  assert(iswalpha(L'A') && iswdigit(L'7') && towupper(L'a') == L'A');

  /* wchar: ワイド文字列 (libc) */
  wchar_t ws[] = L"hello";
  assert(wcslen(ws) == 5);
  wchar_t wd[8];
  wcscpy(wd, ws);
  assert(wcscmp(ws, wd) == 0);

  /* tgmath: 型総称マクロ (float/double を引数型でディスパッチ、libc) */
  double dr = sqrt(2.0);
  float  fr = sqrt(2.0f);
  assert((int)(dr * 1000) == 1414);
  assert((int)(fr * 1000) == 1414);
  assert((int)pow(2.0, 10.0) == 1024);
  assert((int)fabs(-3.5) == 3);

  return 0;
}

noreturn void die(void) { while (1) {} }
