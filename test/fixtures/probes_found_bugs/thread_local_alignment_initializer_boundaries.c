// C11 thread-local data/BSS alignment and constant initializer boundaries.
#include <assert.h>
#include <complex.h>
#include <stdint.h>
#include <uchar.h>

static int global_value = 41;
static char global_text[] = "thread";

struct payload {
  int *pointer;
  char *text;
  double complex value;
  char16_t label[4];
  int canary;
};

/*
 * Keep the one-byte objects after the over-aligned declarations. The global
 * registry emits objects in reverse declaration order, so these become
 * deliberate odd-sized predecessors in the same TLS sections.
 */
_Alignas(16) _Thread_local struct payload initialized_payload = {
    &global_value, global_text + 1, 3.5 + 4.25 * I, u"ok", 0x12345678};
_Thread_local unsigned char initialized_shift = 0xa5;

_Alignas(16) _Thread_local unsigned char zero_payload[129];
_Thread_local unsigned char zero_shift;

static struct payload *block_payload(void) {
  static _Alignas(16) _Thread_local struct payload value = {
      &global_value, global_text + 2, -6.0 + 7.0 * I, u"in", 0x24681357};
  static _Thread_local unsigned char shift = 0x5a;
  assert(shift == 0x5a);
  return &value;
}

static int payload_matches(
    const struct payload *value, char *text, double complex complex_value,
    char16_t first, char16_t second, int canary) {
  return value->pointer == &global_value &&
         *value->pointer == 41 &&
         value->text == text &&
         value->value == complex_value &&
         value->label[0] == first &&
         value->label[1] == second &&
         value->label[2] == 0 &&
         value->canary == canary;
}

static uintptr_t pointer_mod(void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment;
}

int main(void) {
  struct payload *local = block_payload();

  /* Keep the check behind an opaque pointer parameter so the declaration's
   * promised alignment cannot constant-fold the runtime placement check. */
  assert(pointer_mod(&initialized_payload, 16) == 0);
  assert(pointer_mod(&zero_payload, 16) == 0);
  assert(pointer_mod(local, 16) == 0);

  assert(initialized_shift == 0xa5);
  assert(zero_shift == 0);
  assert(zero_payload[0] == 0);
  assert(zero_payload[64] == 0);
  assert(zero_payload[128] == 0);

  assert(payload_matches(
      &initialized_payload, global_text + 1, 3.5 + 4.25 * I,
      (char16_t)'o', (char16_t)'k', 0x12345678));
  assert(payload_matches(
      local, global_text + 2, -6.0 + 7.0 * I,
      (char16_t)'i', (char16_t)'n', 0x24681357));

  initialized_payload.value += 2.0 - 1.0 * I;
  zero_payload[64] = 73;
  local->canary = 0x10203040;
  assert(initialized_payload.value == 5.5 + 3.25 * I);
  assert(zero_payload[64] == 73);
  assert(block_payload()->canary == 0x10203040);
  return 0;
}
