/*
 * Preserve every C11 ATOMIC_*_LOCK_FREE macro as an int integer constant
 * expression and keep it consistent with atomic_is_lock_free for the
 * corresponding basic types and pointer representations.
 */
#include <assert.h>
#include <stdatomic.h>
#include <stdatomic.h>

#if ATOMIC_BOOL_LOCK_FREE != 2 || ATOMIC_CHAR_LOCK_FREE != 2 || \
    ATOMIC_CHAR16_T_LOCK_FREE != 2 || ATOMIC_CHAR32_T_LOCK_FREE != 2 || \
    ATOMIC_WCHAR_T_LOCK_FREE != 2 || ATOMIC_SHORT_LOCK_FREE != 2 || \
    ATOMIC_INT_LOCK_FREE != 2 || ATOMIC_LONG_LOCK_FREE != 2 || \
    ATOMIC_LLONG_LOCK_FREE != 2 || ATOMIC_POINTER_LOCK_FREE != 2
#error "unexpected target lock-free guarantees"
#endif

#define IS_INT(expression) _Generic((expression), int: 1, default: 0)

_Static_assert(IS_INT(ATOMIC_BOOL_LOCK_FREE), "bool macro type");
_Static_assert(IS_INT(ATOMIC_CHAR_LOCK_FREE), "char macro type");
_Static_assert(IS_INT(ATOMIC_CHAR16_T_LOCK_FREE), "char16 macro type");
_Static_assert(IS_INT(ATOMIC_CHAR32_T_LOCK_FREE), "char32 macro type");
_Static_assert(IS_INT(ATOMIC_WCHAR_T_LOCK_FREE), "wchar macro type");
_Static_assert(IS_INT(ATOMIC_SHORT_LOCK_FREE), "short macro type");
_Static_assert(IS_INT(ATOMIC_INT_LOCK_FREE), "int macro type");
_Static_assert(IS_INT(ATOMIC_LONG_LOCK_FREE), "long macro type");
_Static_assert(IS_INT(ATOMIC_LLONG_LOCK_FREE), "long long macro type");
_Static_assert(IS_INT(ATOMIC_POINTER_LOCK_FREE), "pointer macro type");

enum {
  lock_free_macro_sum =
      ATOMIC_BOOL_LOCK_FREE + ATOMIC_CHAR_LOCK_FREE +
      ATOMIC_CHAR16_T_LOCK_FREE + ATOMIC_CHAR32_T_LOCK_FREE +
      ATOMIC_WCHAR_T_LOCK_FREE + ATOMIC_SHORT_LOCK_FREE +
      ATOMIC_INT_LOCK_FREE + ATOMIC_LONG_LOCK_FREE +
      ATOMIC_LLONG_LOCK_FREE + ATOMIC_POINTER_LOCK_FREE,
};

typedef int (*function_pointer)(void);

int main(void) {
  atomic_bool bool_value = ATOMIC_VAR_INIT(0);
  atomic_char char_value = ATOMIC_VAR_INIT(0);
  atomic_char16_t char16_value = ATOMIC_VAR_INIT(0);
  atomic_char32_t char32_value = ATOMIC_VAR_INIT(0);
  atomic_wchar_t wchar_value = ATOMIC_VAR_INIT(0);
  atomic_short short_value = ATOMIC_VAR_INIT(0);
  atomic_int int_value = ATOMIC_VAR_INIT(0);
  atomic_long long_value = ATOMIC_VAR_INIT(0);
  atomic_llong long_long_value = ATOMIC_VAR_INIT(0);
  _Atomic(void *) data_pointer = ATOMIC_VAR_INIT(0);
  _Atomic(function_pointer) function_value = ATOMIC_VAR_INIT(0);

  assert(lock_free_macro_sum == 20);
  assert(atomic_is_lock_free(&bool_value));
  assert(atomic_is_lock_free(&char_value));
  assert(atomic_is_lock_free(&char16_value));
  assert(atomic_is_lock_free(&char32_value));
  assert(atomic_is_lock_free(&wchar_value));
  assert(atomic_is_lock_free(&short_value));
  assert(atomic_is_lock_free(&int_value));
  assert(atomic_is_lock_free(&long_value));
  assert(atomic_is_lock_free(&long_long_value));
  assert(atomic_is_lock_free(&data_pointer));
  assert(atomic_is_lock_free(&function_value));
  return 0;
}
