/*
 * TIME_UTC is the C11-defined base for timespec_get.  A successful call
 * returns that base and produces a normalized nanosecond field; an
 * environment without a calendar clock may report failure by returning zero.
 */
#include <assert.h>
#include <time.h>

_Static_assert(TIME_UTC > 0, "TIME_UTC is a positive base identifier");

int main(void) {
  int (*read_time)(struct timespec *, int) = timespec_get;
  struct timespec direct = {0, 0};
  struct timespec indirect = {0, 0};
  int direct_result;
  int indirect_result;

  assert(read_time != 0);
  direct_result = timespec_get(&direct, TIME_UTC);
  assert(direct_result == 0 || direct_result == TIME_UTC);
  if (direct_result == TIME_UTC) {
    assert(direct.tv_nsec >= 0);
    assert(direct.tv_nsec < 1000000000L);
  }

  indirect_result = read_time(&indirect, TIME_UTC);
  assert(indirect_result == 0 || indirect_result == TIME_UTC);
  if (indirect_result == TIME_UTC) {
    assert(indirect.tv_nsec >= 0);
    assert(indirect.tv_nsec < 1000000000L);
  }
  return 0;
}
