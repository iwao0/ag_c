// math.h f/l wrappers should emit the base WAT stubs they call.
// expected: exit=0
#include <assert.h>
#include <math.h>

int main(void) {
  int qi = 0;
  long double whole = 0.0L;
  float rf = remainderf(5.5f, 2.0f);
  long double rl = remainderl(5.5L, 2.0L);
  float rqf = remquof(5.5f, 2.0f, &qi);
  long double rql;
  long double frac;

  assert(rf < -0.49f && rf > -0.51f);
  assert(rl < -0.49L && rl > -0.51L);
  assert(rqf < -0.49f && rqf > -0.51f && qi == 3);
  qi = 0;
  rql = remquol(5.5L, 2.0L, &qi);
  assert(rql < -0.49L && rql > -0.51L && qi == 3);

  frac = modfl(-2.25L, &whole);
  assert(frac < -0.24L && frac > -0.26L);
  assert((int)whole == -2);

  assert(scalbnf(0.5f, 5) > 15.9f && scalbnf(0.5f, 5) < 16.1f);
  assert(scalbnl(0.25L, 6) > 15.9L && scalbnl(0.25L, 6) < 16.1L);
  assert(scalblnf(1.25f, 2L) > 4.9f && scalblnf(1.25f, 2L) < 5.1f);
  assert(scalblnl(3.0L, -1L) > 1.4L && scalblnl(3.0L, -1L) < 1.6L);
  assert(ldexpf(0.5f, 5) > 15.9f && ldexpf(0.5f, 5) < 16.1f);
  assert(ldexpl(0.25L, 6) > 15.9L && ldexpl(0.25L, 6) < 16.1L);

  assert(ilogbf(0.75f) == -1);
  assert(ilogbl(0.25L) == -2);
  assert((int)logbf(0.75f) == -1);
  assert((int)logbl(0.25L) == -2);
  return 0;
}
