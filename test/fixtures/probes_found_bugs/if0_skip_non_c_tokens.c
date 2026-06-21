/* `#if 0` 偽分岐内の非C トークンの読み飛ばし (C11 6.10.1: 偽分岐は字句のみ走査し
 * フェーズ 7 へ渡さない)。ag_c は全ファイルを先にトークナイズしてから前処理するため、
 * 偽分岐内のトークナイズ不能文字 (` @ $)・未終端リテラル (`don't` の `'`、閉じない `"`)・
 * 不正数値 (123abc) で E2028/E2018 等を出してしまっていた。寛容モードを設け、skip と
 * 行末先読みの間だけ scanner エラーを longjmp で受けて当該トークンを TK_UNKNOWN にする。
 * active コードの同じ不正は従来どおりエラー (フラグ OFF)。 */
#include <assert.h>

#if 0
`@$ これはCではないトークンとUTF-8 ¥ §
don't won't can't  -- 未終端の文字リテラル (アポストロフィ)
"閉じない文字列リテラル
123abc  0x  1.2.3  99999999999999999999999  -- 不正な数値
"\q 不正なエスケープ"
#if 0
ネストした偽分岐 $@`
#endif
#endif

int g_active = 11;

#if 0
more garbage @@@ `` after a nested-and-popped block
#elif 0
$ elif also skipped `bad
#else
int g_else = 22;   /* この else 分岐が採用される */
#endif

int main(void) {
  assert(g_active == 11);
  assert(g_else == 22);
  /* 偽分岐をまたいでも後続の通常コードが正しくトークナイズ・コンパイルされる */
  int n = 0;
#if 0
  n = 999;  /* skipped */
  ' unterminated and `weird
#endif
  n += 5;
  assert(n == 5);
  return 0;
}
