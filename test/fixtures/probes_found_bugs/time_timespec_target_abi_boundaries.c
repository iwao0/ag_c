/*
 * Preserve struct timespec's public ABI and the write boundary used by the
 * native libc and the standalone Wasm runtimes.
 */
#include <assert.h>
#include <stddef.h>
#include <time.h>

_Static_assert(_Generic(((struct timespec *)0)->tv_sec,
                        time_t: 1, default: 0),
               "timespec tv_sec type");
_Static_assert(_Generic(((struct timespec *)0)->tv_nsec,
                        long: 1, default: 0),
               "timespec tv_nsec type");
_Static_assert(offsetof(struct timespec, tv_sec) == 0,
               "timespec tv_sec offset");
_Static_assert(offsetof(struct timespec, tv_nsec) == 8,
               "timespec tv_nsec offset");
_Static_assert(sizeof(struct timespec) == 16, "timespec size");
_Static_assert(_Alignof(struct timespec) == 8, "timespec alignment");

static int (*timespec_get_signature)(struct timespec *, int) = timespec_get;

struct guarded_timespec {
  unsigned long before;
  struct timespec value;
  unsigned long after;
};

static void initialize_guarded(struct guarded_timespec *guarded) {
  guarded->before = 0x13579bdf2468ace0UL;
  guarded->value.tv_sec = 0x1122334455667788L;
  guarded->value.tv_nsec = 0x2233445566778899L;
  guarded->after = 0x02468ace13579bdfUL;
}

static void assert_canaries(const struct guarded_timespec *guarded) {
  assert(guarded->before == 0x13579bdf2468ace0UL);
  assert(guarded->after == 0x02468ace13579bdfUL);
}

int main(void) {
  struct guarded_timespec valid;
  struct guarded_timespec invalid;
  int result;

  assert(timespec_get_signature != NULL);

  initialize_guarded(&invalid);
  assert(timespec_get_signature(&invalid.value, -1) == 0);
  assert(invalid.value.tv_sec == 0x1122334455667788L);
  assert(invalid.value.tv_nsec == 0x2233445566778899L);
  assert_canaries(&invalid);

  initialize_guarded(&valid);
  result = timespec_get(&valid.value, TIME_UTC);
  assert(result == 0 || result == TIME_UTC);
  if (result == TIME_UTC) {
    assert(valid.value.tv_nsec >= 0);
    assert(valid.value.tv_nsec < 1000000000L);
  } else {
    assert(valid.value.tv_sec == 0x1122334455667788L);
    assert(valid.value.tv_nsec == 0x2233445566778899L);
  }
  assert_canaries(&valid);
  return 0;
}
