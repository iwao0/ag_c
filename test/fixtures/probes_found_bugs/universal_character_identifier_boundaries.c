/*
 * Universal character names and their UTF-8 source spelling denote the same
 * identifier in every C identifier namespace.  Exercise macro expansion,
 * ordinary/tag/member names, labels, typedefs, and emitted function symbols.
 */
#include <assert.h>

#define \u03A9_MACRO 5
#define CAT_RAW(left, right) left ## right
#define CAT(left, right) CAT_RAW(left, right)

static int caf\u00E9 = 7;
static int \u03A9value = 11;

struct \u03C1 {
  int \u03B1;
  int β;
};

enum \u03B5 {
  \u03BA = 13
};

typedef int \u03C4;

static int \u03BB(int value) {
  return value + Ω_MACRO;
}

int main(void) {
  int na\u00EFve = 17;
  τ typed = 19;
  struct ρ pair = {.\u03B1 = typed, .β = 23};
  int (*operation)(int) = \u03BB;

  assert(naïve == 17);
  assert(café == 7);
  assert(pair.α == 19);
  assert(pair.\u03B2 == 23);
  assert(κ == 13);
  assert(λ(37) == 42);
  assert(operation(36) == 41);
  assert(CAT(Ω, value) == 11);

  goto \u03B4;
  return 1;
δ:
  return 0;
}
