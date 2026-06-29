/* UTF-16/UTF-32/wide 文字列リテラル `u"..."` / `U"..."` / `L"..."` での配列初期化。
 * 以前は文字定数 `u'A'`/`U'A'`/`L'A'` は通るのに、文字列を配列初期化子に使うと:
 *   - 明示サイズ `unsigned short s[3]=u"hi"` → 値が格納されず 0 に化ける (miscompile)
 *   - サイズ省略 `unsigned short s[]=u"hi"` → E3064 (配列サイズ推論失敗)
 *   - グローバル `unsigned short g[]=u"hi"` → .comm 0 (サイズ 0)
 * いずれも「char(1B) 配列のみ」を前提とした 3 経路 (ローカル init / [] 推論 / グローバル init) が
 * 要素幅 1 に決め打ちだったため。要素幅 (elem_size) が文字列の char_width (char/u8=1, u=2,
 * U/L=4) と一致するときに各コード単位を要素幅で格納するよう一般化した。
 * 続き5で UTF-8 ソースをコードポイントへデコードし、非 ASCII の UTF-16/UTF-32 も対応済み。 */
#include <assert.h>

unsigned short gu[]  = u"hi";      /* グローバル UTF-16, サイズ推論 */
unsigned int   gU[]  = U"abc";     /* グローバル UTF-32 */
char           g8[]  = u8"ok";     /* グローバル UTF-8 (char) */

int main(void) {
  /* u"" UTF-16: 2 バイト要素 */
  unsigned short s[] = u"hi";
  assert(s[0] == 'h' && s[1] == 'i' && s[2] == 0);
  assert(sizeof(s) == 3 * sizeof(unsigned short));
  unsigned short s3[3] = u"AB";     /* 明示サイズ */
  assert(s3[0] == 'A' && s3[1] == 'B' && s3[2] == 0);

  /* U"" / L"" UTF-32: 4 バイト要素 */
  unsigned int u[] = U"abc";
  assert(u[0] == 'a' && u[2] == 'c' && u[3] == 0);
  int w[] = L"xy";                  /* wchar_t は 4B (darwin) */
  assert(w[0] == 'x' && w[1] == 'y' && w[2] == 0);

  /* u8"" は char 配列 */
  char c[] = u8"hi";
  assert(c[0] == 'h' && sizeof(c) == 3);

  /* ポインタ経由 */
  unsigned short *p = u"PQ";
  assert(p[0] == 'P' && p[1] == 'Q');

  /* sizeof of literal */
  assert(sizeof(u"hello") == 6 * sizeof(unsigned short));
  assert(sizeof(U"hi") == 3 * sizeof(unsigned int));
  assert(sizeof("ok") == 3);

  /* グローバル */
  assert(gu[0] == 'h' && gu[1] == 'i' && gu[2] == 0);
  assert(gU[0] == 'a' && gU[2] == 'c' && gU[3] == 0);
  assert(g8[0] == 'o' && g8[2] == 0);

  /* 非 ASCII (UTF-8 ソース → コードポイント)。`あ`=U+3042, `α`=U+03B1。
   * U/L (UTF-32) は 1 コードポイント = 1 要素、u (UTF-16) は BMP なので 1 要素。 */
  unsigned int u32[] = U"aあb";
  assert(u32[0] == 0x61 && u32[1] == 0x3042 && u32[2] == 0x62 && u32[3] == 0);
  assert(sizeof(u32) == 4 * sizeof(unsigned int));
  unsigned short u16[] = u"aあb";
  assert(u16[0] == 0x61 && u16[1] == 0x3042 && u16[2] == 0x62 && u16[3] == 0);
  assert(sizeof(u16) == 4 * sizeof(unsigned short));
  /* u8 は UTF-8 バイト列のまま (`あ` は 3 バイト) */
  char u8b[] = u8"aあ";
  assert(sizeof(u8b) == 1 + 3 + 1);

  return 0;
}
