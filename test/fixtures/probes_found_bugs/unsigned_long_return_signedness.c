/* `unsigned long` / `unsigned char` を返す関数の戻り値の符号性。
 * bug_coverage で ⚠️ (plain unsigned のみ追跡、ret_token_kind が TK_LONG/CHAR に潰れる)
 * とされていたが、再検証では shift / divide / 比較いずれも clang と一致し再現しない。
 * 回帰として固定する。 */
#include <assert.h>

unsigned long ful_big(void){ return 0xFFFFFFFFFFFFFFFFUL; }
unsigned long ful_half(void){ return 0x8000000000000000UL; }
unsigned char fuc(void){ return 200; }
unsigned long long full_big(void){ return 0xFFFFFFFFFFFFFFFFULL; }

int main(void){
  /* 符号性で結果が変わる演算 */
  assert((ful_big() >> 1) == 0x7FFFFFFFFFFFFFFFUL);   /* 論理シフト (算術なら 0xFFFF..) */
  assert((ful_half() >> 1) == 0x4000000000000000UL);
  assert((ful_big() / 2) == 0x7FFFFFFFFFFFFFFFUL);    /* 符号なし除算 */
  assert((full_big() >> 1) == 0x7FFFFFFFFFFFFFFFULL);
  /* 比較: 符号なしなら huge > 1 は真、符号付き(=-1)なら偽 */
  assert(ful_big() > 1);
  assert(!(ful_big() < 1));
  /* unsigned char 戻り (int 昇格しても値は 200) */
  assert(fuc() == 200);
  unsigned long u = fuc();
  assert(u == 200);
  return 0;
}
