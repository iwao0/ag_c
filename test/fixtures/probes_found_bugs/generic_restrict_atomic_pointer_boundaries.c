/*
 * Generic selection removes a pointer value's top-level cv/restrict
 * qualifiers.  Qualifiers on an inner pointer and an atomic pointer type
 * remain part of the type identity.  The controlling expression is never
 * evaluated.
 */
#include <assert.h>

#define TYPE_IS(expression, type) \
  _Generic((expression), type: 1, default: 0)

typedef int *restrict RestrictedIntPointer;
typedef int *const restrict ConstRestrictedIntPointer;
typedef int *volatile restrict VolatileRestrictedIntPointer;
typedef int *restrict *PointerToRestrictedIntPointer;
typedef PointerToRestrictedIntPointer const
    ConstPointerToRestrictedIntPointer;
typedef _Atomic(int *) AtomicIntPointer;
typedef AtomicIntPointer *restrict RestrictedPointerToAtomicIntPointer;
typedef RestrictedPointerToAtomicIntPointer const
    ConstRestrictedPointerToAtomicIntPointer;

static int global_value = 31;
static RestrictedIntPointer global_pointer = &global_value;

struct pointer_box {
  RestrictedIntPointer pointer;
  AtomicIntPointer atomic_pointer;
};

_Static_assert(TYPE_IS((RestrictedIntPointer)0, int *),
               "top-level restrict is removed from a cast value");
_Static_assert(TYPE_IS((ConstRestrictedIntPointer)0, int *),
               "top-level const and restrict are removed together");
_Static_assert(TYPE_IS((VolatileRestrictedIntPointer)0, int *),
               "top-level volatile and restrict are removed together");
_Static_assert(TYPE_IS((PointerToRestrictedIntPointer)0,
                       int *restrict *),
               "restrict on an inner pointer remains distinct");
_Static_assert(TYPE_IS((ConstPointerToRestrictedIntPointer)0,
                       int *restrict *),
               "only the outer const qualifier is removed");
_Static_assert(TYPE_IS((AtomicIntPointer)0, AtomicIntPointer),
               "an atomic pointer cast retains atomic type identity");
_Static_assert(TYPE_IS((RestrictedPointerToAtomicIntPointer)0,
                       AtomicIntPointer *),
               "outer restrict is removed while inner atomic remains");
_Static_assert(TYPE_IS((ConstRestrictedPointerToAtomicIntPointer)0,
                       AtomicIntPointer *),
               "outer const and restrict do not erase inner atomic");

int main(void) {
  int value = 17;
  RestrictedIntPointer pointer = &value;
  PointerToRestrictedIntPointer pointer_to_pointer = &pointer;
  AtomicIntPointer atomic_pointer = &value;
  RestrictedPointerToAtomicIntPointer pointer_to_atomic = &atomic_pointer;
  struct pointer_box box = {&value, &global_value};
  int effects = 0;

  assert(TYPE_IS(pointer, int *));
  assert(TYPE_IS(&pointer, int *restrict *));
  assert(TYPE_IS(pointer_to_pointer, int *restrict *));
  assert(TYPE_IS(atomic_pointer, int *));
  assert(TYPE_IS(&atomic_pointer, AtomicIntPointer *));
  assert(TYPE_IS(pointer_to_atomic, AtomicIntPointer *));
  assert(TYPE_IS(box.pointer, int *));
  assert(TYPE_IS(&box.pointer, int *restrict *));
  assert(TYPE_IS(box.atomic_pointer, int *));
  assert(TYPE_IS(&box.atomic_pointer, AtomicIntPointer *));

  assert(_Generic((ConstRestrictedIntPointer)(effects++, &value),
                  int *: 41, default: 0) == 41);
  assert(effects == 0);
  assert(_Generic((ConstPointerToRestrictedIntPointer)
                      (effects++, pointer_to_pointer),
                  int *restrict *: 42, default: 0) == 42);
  assert(effects == 0);
  assert(_Generic((ConstRestrictedPointerToAtomicIntPointer)
                      (effects++, pointer_to_atomic),
                  AtomicIntPointer *: 43, default: 0) == 43);
  assert(effects == 0);

  assert(*pointer == 17);
  assert(**pointer_to_pointer == 17);
  assert(*atomic_pointer == 17);
  assert(**pointer_to_atomic == 17);
  assert(*box.pointer == 17);
  assert(*box.atomic_pointer == 31);
  assert(*global_pointer == 31);
  return 0;
}
