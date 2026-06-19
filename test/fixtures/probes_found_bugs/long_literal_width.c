// long / long long サフィックス付き整数リテラルが、値が 32bit に収まると i32 として
// 扱われ、`u * 2L` や `i * 2L` が 32bit 演算になり 2^32 超で wrap していた。
// parser が token の int_size (long サフィックス) を node_num_t.int_is_long へ伝え、
// build_node_num が long リテラルを i64 で生成するよう修正。
#include <assert.h>
int main(void) {
  int t = 0;

  // 小さい値の long リテラルとの演算が 64bit になる
  unsigned u = 3000000000u;
  t += (u * 2L == 6000000000L);       // unsigned int * 2L

  int i = 2000000000;
  t += (i * 2L == 4000000000L);       // signed int * 2L (overflows int)

  t += (1000000 * 1000000L == 1000000000000L);
  t += (1L << 40 == 1099511627776L);  // shift of small long literal
  t += (5L * 5L == 25L);

  // long long / unsigned long リテラル
  t += (1LL << 50 == 1125899906842624LL);
  unsigned long ul = 10UL;
  t += (ul * 1000000000UL == 10000000000UL);

  // 通常の int リテラルは従来通り (32bit wrap)
  unsigned w = 3000000000u;
  t += (w * 2u == 1705032704u);       // unsigned int * 2u wraps at 2^32

  assert(t == 8); return 0;  // 8 checks -> 8+34 = 42
}
