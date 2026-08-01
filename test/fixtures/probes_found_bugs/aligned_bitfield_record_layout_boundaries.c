/*
 * An over-aligned ordinary member terminates the preceding bit-field
 * allocation unit and raises the alignment of the complete record.  A later
 * zero-width bit-field starts the next named field in a fresh allocation
 * unit.  Keep those layout decisions consistent for static and automatic
 * objects, nested records, and arrays.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

struct AlignedBits {
  unsigned int low : 3;
  unsigned int high : 5;
  _Alignas(16) unsigned int anchor;
  unsigned int first : 7;
  unsigned int : 0;
  unsigned int second : 9;
  unsigned char tail;
};

struct AlignedBitsEnvelope {
  unsigned char before;
  struct AlignedBits value;
  unsigned char after;
};

_Static_assert(_Alignof(struct AlignedBits) == 16,
               "aligned member raises record alignment");
_Static_assert(offsetof(struct AlignedBits, anchor) == 16,
               "aligned member begins at a 16-byte boundary");
_Static_assert(offsetof(struct AlignedBits, tail) == 26,
               "zero-width field separates later allocation unit");
_Static_assert(sizeof(struct AlignedBits) == 32,
               "record size includes tail padding");
_Static_assert(offsetof(struct AlignedBitsEnvelope, value) == 16,
               "nested record preserves alignment");
_Static_assert(offsetof(struct AlignedBitsEnvelope, after) == 48,
               "nested record preserves size");
_Static_assert(sizeof(struct AlignedBitsEnvelope) == 64,
               "envelope tail padding preserves array stride");

static struct AlignedBits global_value = {5, 26, 0x12345678u, 101, 341, 0xa5};
static struct AlignedBits global_array[2] = {
    {1, 2, 11, 3, 4, 5},
    {6, 31, 22, 127, 511, 0xfe},
};

static int check_value(const struct AlignedBits *value, int low, int high,
                       unsigned int anchor, int first, int second, int tail) {
  return value->low == low && value->high == high &&
         value->anchor == anchor && value->first == first &&
         value->second == second && value->tail == tail;
}

static struct AlignedBits *static_value(void) {
  static struct AlignedBits value = {7, 17, 99, 65, 257, 0x7d};
  return &value;
}

int main(void) {
  struct AlignedBits automatic = {2, 29, 0xabcdef01u, 97, 300, 0xc3};
  struct AlignedBits copied = automatic;
  struct AlignedBitsEnvelope envelope = {
      0x11, {4, 23, 77, 88, 400, 0x99}, 0x22};

  assert(check_value(&global_value, 5, 26, 0x12345678u, 101, 341, 0xa5));
  assert(check_value(&global_array[0], 1, 2, 11, 3, 4, 5));
  assert(check_value(&global_array[1], 6, 31, 22, 127, 511, 0xfe));
  assert(check_value(&automatic, 2, 29, 0xabcdef01u, 97, 300, 0xc3));
  assert(check_value(&copied, 2, 29, 0xabcdef01u, 97, 300, 0xc3));
  assert(check_value(static_value(), 7, 17, 99, 65, 257, 0x7d));
  assert(envelope.before == 0x11 && envelope.after == 0x22);
  assert(check_value(&envelope.value, 4, 23, 77, 88, 400, 0x99));

  assert((uintptr_t)&global_value % 16u == 0u);
  assert((uintptr_t)&global_array[0] % 16u == 0u);
  assert((uintptr_t)&global_array[1] % 16u == 0u);
  assert((uintptr_t)&automatic % 16u == 0u);
  assert((uintptr_t)&copied % 16u == 0u);
  assert((uintptr_t)static_value() % 16u == 0u);
  assert((uintptr_t)&envelope.value % 16u == 0u);
  assert((unsigned char *)&global_array[1] -
             (unsigned char *)&global_array[0] ==
         (ptrdiff_t)sizeof(struct AlignedBits));
  return 0;
}
