#ifndef _ASSERT_H
#define _ASSERT_H

/* C11 7.2.1.1: 式が偽のとき、引数テキスト・__FILE__・__LINE__・__func__ を
 * 標準エラー出力に処理系定義の形式で書き出し、その後 abort() する。
 * Apple のランタイムが提供する __assert_rtn を呼ぶ (clang の <assert.h> と同一形式・
 * stderr 出力)。従来は abort() のみで診断を書き出しておらず C11 非適合だった。 */
void __assert_rtn(const char *, const char *, int, const char *);

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) \
  ((expr) ? (void)0 : __assert_rtn(__func__, __FILE__, __LINE__, #expr))
#endif

#endif
