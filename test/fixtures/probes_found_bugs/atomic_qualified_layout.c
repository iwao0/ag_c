#include <assert.h>
#include <stddef.h>
#include <stdint.h>

struct bytes3 {
  unsigned char value[3];
};

struct bytes5 {
  unsigned char value[5];
};

struct words3 {
  int value[3];
};

struct atomic_holder {
  unsigned char lead;
  _Atomic(struct bytes3) three;
  _Atomic(struct bytes5) five;
  _Atomic(struct words3) twelve;
  _Atomic(float _Complex) float_complex;
  _Atomic(double _Complex) double_complex;
  unsigned char tail;
};

_Atomic(struct bytes3) global_three;
static _Atomic(struct bytes5) global_five;
_Atomic(struct bytes3) global_three_array[2];
static _Atomic(struct bytes3) global_initialized[2] = {
    {{1, 2, 3}}, {{4, 5, 6}}};

_Static_assert(sizeof(_Atomic(struct bytes3)) == 4,
               "three-byte atomic object must use four-byte storage");
_Static_assert(_Alignof(_Atomic(struct bytes3)) == 4,
               "three-byte atomic object must be four-byte aligned");
_Static_assert(sizeof(_Atomic(struct bytes5)) == 8,
               "five-byte atomic object must use eight-byte storage");
_Static_assert(_Alignof(_Atomic(struct bytes5)) == 8,
               "five-byte atomic object must be eight-byte aligned");
_Static_assert(sizeof(_Atomic(struct words3)) == 16,
               "twelve-byte atomic object must use sixteen-byte storage");
_Static_assert(sizeof(_Atomic(float _Complex)) == 8,
               "atomic float complex storage size");
_Static_assert(_Alignof(_Atomic(float _Complex)) == 8,
               "atomic float complex alignment");
_Static_assert(sizeof(_Atomic(double _Complex)) == 16,
               "atomic double complex storage size");
_Static_assert(sizeof(global_three_array) == 8,
               "array stride must retain the atomic element layout");

#ifdef __wasm32__
_Static_assert(_Alignof(_Atomic(struct words3)) == 8,
               "wasm32 atomic aggregate alignment");
_Static_assert(_Alignof(_Atomic(double _Complex)) == 8,
               "wasm32 atomic double complex alignment");
_Static_assert(sizeof(struct atomic_holder) == 64,
               "wasm32 holder size");
#else
_Static_assert(_Alignof(_Atomic(struct words3)) == 16,
               "host atomic aggregate alignment");
_Static_assert(_Alignof(_Atomic(double _Complex)) == 16,
               "host atomic double complex alignment");
_Static_assert(sizeof(struct atomic_holder) == 80,
               "host holder size");
#endif

static int is_aligned(const void *pointer, size_t alignment) {
  return ((uintptr_t)pointer % alignment) == 0;
}

int main(void) {
  _Atomic(struct bytes3) local_three;
  _Atomic(struct bytes5) local_five;
  _Atomic(struct words3) local_twelve;
  _Atomic(struct bytes3) local_initialized[2] = {
      {{7, 8, 9}}, {{10, 11, 12}}};
  struct atomic_holder holder;
  const unsigned char *global_bytes =
      (const unsigned char *)global_initialized;
  const unsigned char *local_bytes =
      (const unsigned char *)local_initialized;

  assert(is_aligned(&global_three, _Alignof(_Atomic(struct bytes3))));
  assert(is_aligned(&global_five, _Alignof(_Atomic(struct bytes5))));
  assert(is_aligned(&global_three_array[0],
                    _Alignof(_Atomic(struct bytes3))));
  assert((char *)&global_three_array[1] -
             (char *)&global_three_array[0] == 4);
  assert(global_bytes[0] == 1);
  assert(global_bytes[1] == 2);
  assert(global_bytes[2] == 3);
  assert(global_bytes[4] == 4);
  assert(global_bytes[5] == 5);
  assert(global_bytes[6] == 6);
  assert(is_aligned(&local_three, _Alignof(_Atomic(struct bytes3))));
  assert(is_aligned(&local_five, _Alignof(_Atomic(struct bytes5))));
  assert(is_aligned(&local_twelve, _Alignof(_Atomic(struct words3))));
  assert(local_bytes[0] == 7);
  assert(local_bytes[1] == 8);
  assert(local_bytes[2] == 9);
  assert(local_bytes[4] == 10);
  assert(local_bytes[5] == 11);
  assert(local_bytes[6] == 12);
  assert(is_aligned(&holder.three, _Alignof(_Atomic(struct bytes3))));
  assert(is_aligned(&holder.five, _Alignof(_Atomic(struct bytes5))));
  assert(is_aligned(&holder.twelve, _Alignof(_Atomic(struct words3))));
#ifdef __wasm32__
  assert(offsetof(struct atomic_holder, three) == 4);
  assert(offsetof(struct atomic_holder, five) == 8);
  assert(offsetof(struct atomic_holder, twelve) == 16);
  assert(offsetof(struct atomic_holder, float_complex) == 32);
  assert(offsetof(struct atomic_holder, double_complex) == 40);
  assert(offsetof(struct atomic_holder, tail) == 56);
#else
  assert(offsetof(struct atomic_holder, three) == 4);
  assert(offsetof(struct atomic_holder, five) == 8);
  assert(offsetof(struct atomic_holder, twelve) == 16);
  assert(offsetof(struct atomic_holder, float_complex) == 32);
  assert(offsetof(struct atomic_holder, double_complex) == 48);
  assert(offsetof(struct atomic_holder, tail) == 64);
#endif
  return 0;
}
