// Runtime i32 expressions must be sign-extended before signed i64 arithmetic
// and comparisons. AArch64 W-register results otherwise look nonnegative.
// Expected: exit=0
#include <assert.h>

static int amount_source = 3;
static int amount_reads;

static int next_amount(void) {
  amount_reads++;
  return amount_source;
}

int main(void) {
  int amount = next_amount();
  long long negative = -12LL;
  long long positive_alias = 4294967284LL;

  assert(amount_reads == 1);
  assert(negative == -9 - amount);
  assert(-9 - amount == negative);
  assert(negative <= -9 - amount);
  assert(-9 - amount >= negative);
  assert(negative < -8 - amount);
  assert(-8 - amount > negative);
  assert(positive_alias != -9 - amount);
  assert((-9 - amount) != positive_alias);

  assert(negative + (-9 - amount) == -24LL);
  assert((-9 - amount) + negative == -24LL);
  assert(negative - (-9 - amount) == 0LL);
  assert((-9 - amount) - negative == 0LL);
  assert(2LL * (-9 - amount) == -24LL);
  assert((-9 - amount) * 2LL == -24LL);
  assert((negative & (-9 - amount)) == -12LL);
  assert((negative | (-9 - amount)) == -12LL);
  assert((negative ^ (-9 - amount)) == 0LL);
  return 0;
}
