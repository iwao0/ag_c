#include <assert.h>

static _Atomic int selected_values[2];
static _Atomic int global_atomic_value;
static int address_evaluations;
static int rhs_evaluations;

struct atomic_member_holder {
  _Atomic int value;
};

static _Atomic int *selected_value(void) {
  address_evaluations++;
  return &selected_values[0];
}

static int evaluated_rhs(void) {
  rhs_evaluations++;
  return 5;
}

int main(void) {
  _Atomic int value = 96;
  assert((value += 3) == 99);
  assert((value -= 2) == 97);
  assert((value *= 4) == 388);
  assert((value /= 2) == 194);
  assert((value %= 13) == 12);
  assert((value <<= 2) == 48);
  assert((value >>= 1) == 24);
  assert((value &= 31) == 24);
  assert((value ^= 7) == 31);
  assert((value |= 64) == 95);
  assert(value == 95);

  _Atomic unsigned char byte = 250;
  assert((byte += 10) == 4);
  assert(byte == 4);

  _Atomic short word = -3;
  assert((word *= 4) == -12);
  assert(word == -12);

  _Atomic unsigned long long wide = 1;
  assert((wide <<= 40) == (1ULL << 40));
  assert(wide == (1ULL << 40));

  _Atomic _Bool boolean = 1;
  assert((boolean += 1) == 1);
  assert((boolean ^= 1) == 0);
  assert((boolean -= 2) == 1);

  _Atomic float real = 8.0f;
  assert((real += 1.5f) == 9.5f);
  assert((real -= 0.5f) == 9.0f);
  assert((real *= 2.0f) == 18.0f);
  assert((real /= 4.0f) == 4.5f);

  _Atomic int mixed_integer = 10;
  assert((mixed_integer += 2.75) == 12);

  _Atomic double mixed_real = 4.0;
  assert((mixed_real *= 3) == 12.0);

  _Atomic int incremented = 4;
  assert(incremented++ == 4);
  assert(incremented == 5);
  assert(++incremented == 6);
  assert(incremented-- == 6);
  assert(--incremented == 4);
  assert((incremented = 17) == 17);
  assert(incremented == 17);

  _Atomic _Bool incremented_bool = 0;
  assert(++incremented_bool == 1);
  assert(incremented_bool++ == 1);
  assert(incremented_bool == 1);
  incremented_bool = 0;
  assert(incremented_bool-- == 0);
  assert(incremented_bool == 1);
  assert(--incremented_bool == 0);

  _Atomic float incremented_float = 2.0f;
  assert(incremented_float++ == 2.0f);
  assert(++incremented_float == 4.0f);
  assert(incremented_float-- == 4.0f);
  assert(--incremented_float == 2.0f);

  int pointer_values[4] = {0};
  _Atomic(int *) atomic_pointer = pointer_values;
  int *old_pointer = atomic_pointer++;
  assert(old_pointer == pointer_values);
  assert(atomic_pointer == pointer_values + 1);
  assert(++atomic_pointer == pointer_values + 2);
  assert(atomic_pointer-- == pointer_values + 2);
  assert(--atomic_pointer == pointer_values);
  assert((atomic_pointer = pointer_values + 3) == pointer_values + 3);
  int *pointer_snapshot = atomic_pointer;
  assert(pointer_snapshot == pointer_values + 3);

  global_atomic_value = 40;
  assert(global_atomic_value++ == 40);
  assert(global_atomic_value == 41);

  struct atomic_member_holder holder = {5};
  assert(holder.value == 5);
  assert(++holder.value == 6);
  _Atomic int *member_pointer = &holder.value;
  assert((*member_pointer = 8) == 8);
  assert((*member_pointer)++ == 8);
  assert(holder.value == 9);

  selected_values[0] = 7;
  assert((*selected_value() += evaluated_rhs()) == 12);
  assert(selected_values[0] == 12);
  assert(address_evaluations == 1);
  assert(rhs_evaluations == 1);
  return 0;
}
