/*
 * Explicit alignment on atomic objects and atomic aggregate members must be
 * reflected in every storage duration without changing the atomic value
 * width or the surrounding record layout.
 */
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

struct aligned_atomic_box {
  unsigned char prefix;
  _Alignas(32) _Atomic(unsigned long long) value;
  unsigned char suffix;
};

struct aligned_atomic_outer {
  unsigned char lead;
  struct aligned_atomic_box box;
  unsigned short tail;
};

_Static_assert(_Alignof(struct aligned_atomic_box) == 32,
               "aligned atomic member controls record alignment");
_Static_assert(offsetof(struct aligned_atomic_box, value) == 32,
               "atomic member begins at its explicit alignment");
_Static_assert(offsetof(struct aligned_atomic_box, suffix) == 40,
               "atomic member keeps its eight-byte storage width");
_Static_assert(sizeof(struct aligned_atomic_box) == 64,
               "record size includes alignment tail padding");
_Static_assert(_Alignof(struct aligned_atomic_outer) == 32,
               "nested record retains member alignment");
_Static_assert(offsetof(struct aligned_atomic_outer, box) == 32,
               "nested aligned record offset");
_Static_assert(offsetof(struct aligned_atomic_outer, tail) == 96,
               "member after nested aligned record");
_Static_assert(sizeof(struct aligned_atomic_outer) == 128,
               "nested record size rounds to aggregate alignment");

_Alignas(64) static _Atomic(unsigned long long) global_counter = 11;
static struct aligned_atomic_box global_box = {1, 101, 2};
static struct aligned_atomic_box global_boxes[2] = {
    {3, 103, 4},
    {5, 107, 6},
};
static struct aligned_atomic_outer global_outer = {
    7, {8, 109, 9}, 10,
};

static int is_aligned(const void *address, uintptr_t alignment) {
  return (uintptr_t)address % alignment == (uintptr_t)0;
}

static unsigned long long increment_atomic(
    _Atomic(unsigned long long) *value,
    unsigned long long amount) {
  return atomic_fetch_add_explicit(value, amount, memory_order_relaxed);
}

static void verify_global_storage(void) {
  assert(is_aligned(&global_counter, 64));
  assert(is_aligned(&global_box, 32));
  assert(is_aligned(&global_box.value, 32));
  assert(is_aligned(&global_boxes[0], 32));
  assert(is_aligned(&global_boxes[1], 32));
  assert((unsigned char *)&global_boxes[1] -
             (unsigned char *)&global_boxes[0] ==
         (ptrdiff_t)sizeof(global_boxes[0]));
  assert(is_aligned(&global_outer, 32));
  assert(is_aligned(&global_outer.box.value, 32));

  assert(atomic_load(&global_counter) == 11ULL);
  assert(atomic_exchange(&global_counter, 13ULL) == 11ULL);
  assert(increment_atomic(&global_counter, 5ULL) == 13ULL);
  assert(atomic_load(&global_counter) == 18ULL);

  assert(global_box.prefix == 1 && global_box.suffix == 2);
  assert(atomic_load(&global_box.value) == 101ULL);
  atomic_store(&global_box.value, 113ULL);
  assert(atomic_load(&global_box.value) == 113ULL);
  assert(atomic_load(&global_boxes[0].value) == 103ULL);
  assert(atomic_load(&global_boxes[1].value) == 107ULL);
  assert(global_outer.lead == 7 && global_outer.tail == 10);
  assert(global_outer.box.prefix == 8 && global_outer.box.suffix == 9);
  assert(atomic_load(&global_outer.box.value) == 109ULL);
}

static void verify_automatic_storage(void) {
  _Alignas(64) _Atomic(unsigned long long) local_counter = 127;
  struct aligned_atomic_box local_box = {11, 131, 12};
  struct aligned_atomic_box local_boxes[2] = {
      {13, 137, 14},
      {15, 139, 16},
  };
  struct aligned_atomic_outer local_outer = {
      17, {18, 149, 19}, 20,
  };

  assert(is_aligned(&local_counter, 64));
  assert(is_aligned(&local_box.value, 32));
  assert(is_aligned(&local_boxes[0], 32));
  assert(is_aligned(&local_boxes[1], 32));
  assert(is_aligned(&local_outer.box.value, 32));

  assert(atomic_load(&local_counter) == 127ULL);
  assert(increment_atomic(&local_counter, 10ULL) == 127ULL);
  assert(atomic_load(&local_counter) == 137ULL);
  assert(atomic_exchange(&local_box.value, 151ULL) == 131ULL);
  assert(local_box.prefix == 11 && local_box.suffix == 12);
  assert(atomic_load(&local_box.value) == 151ULL);
  assert(atomic_load(&local_boxes[0].value) == 137ULL);
  assert(atomic_load(&local_boxes[1].value) == 139ULL);
  assert(atomic_load(&local_outer.box.value) == 149ULL);
}

static void verify_static_local_storage(void) {
  _Alignas(64) static _Atomic(unsigned long long) static_counter = 157;
  static struct aligned_atomic_box static_box = {21, 163, 22};

  assert(is_aligned(&static_counter, 64));
  assert(is_aligned(&static_box.value, 32));
  assert(atomic_fetch_add(&static_counter, 2ULL) == 157ULL);
  assert(atomic_load(&static_counter) == 159ULL);
  assert(atomic_exchange(&static_box.value, 167ULL) == 163ULL);
  assert(atomic_load(&static_box.value) == 167ULL);
  assert(static_box.prefix == 21 && static_box.suffix == 22);
}

int main(void) {
  verify_global_storage();
  verify_automatic_storage();
  verify_static_local_storage();
  return 0;
}
