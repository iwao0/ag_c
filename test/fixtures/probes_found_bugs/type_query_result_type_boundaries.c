/*
 * C11 6.5.3.4 specifies size_t as the result type of both sizeof and
 * _Alignof.  Preserve that target-dependent type while querying qualified,
 * atomic, and register-array object types.
 */
#include <assert.h>
#include <stddef.h>

static _Atomic(int) atomic_value;

_Static_assert(
    _Generic(sizeof(int), size_t: 1, default: 0),
    "sizeof result has type size_t");
_Static_assert(
    _Generic(_Alignof(int), size_t: 1, default: 0),
    "_Alignof result has type size_t");
_Static_assert(
    sizeof(const volatile int) == sizeof(int),
    "cv qualifiers do not change object size");
_Static_assert(
    _Alignof(const volatile int) == _Alignof(int),
    "cv qualifiers do not change object alignment");
_Static_assert(
    sizeof atomic_value == sizeof(_Atomic(int)),
    "atomic expression and type-name sizes agree");
_Static_assert(
    _Alignof(_Atomic(int)) > 0,
    "atomic type has a valid alignment");

static int check_register_array(void) {
  register int values[3] = {1, 2, 3};
  return sizeof(values) == 3 * sizeof(int);
}

int main(void) {
  assert(check_register_array());
  assert(atomic_value == 0);
  assert(sizeof atomic_value == sizeof(_Atomic(int)));
  return 0;
}
