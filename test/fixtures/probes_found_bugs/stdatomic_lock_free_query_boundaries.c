// stdatomic atomic_is_lock_free type, width, and evaluation boundaries.
// Expected: exit=0
#include <stdatomic.h>

struct bytes3 {
  unsigned char value[3];
};

struct bytes5 {
  unsigned char value[5];
};

struct bytes12 {
  unsigned char value[12];
};

struct bytes17 {
  unsigned char value[17];
};

struct bytes24 {
  unsigned char value[24];
};

static const volatile atomic_int scalar = ATOMIC_VAR_INIT(1);
static _Atomic(struct bytes3) three;
static _Atomic(struct bytes5) five;
static _Atomic(struct bytes12) twelve;
static _Atomic(struct bytes17) seventeen;
static _Atomic(struct bytes24) twenty_four;
static int object_evaluations;

static const volatile atomic_int *selected_scalar(void) {
  object_evaluations++;
  return &scalar;
}

int main(void) {
  if (!atomic_is_lock_free(&scalar) ||
      !atomic_is_lock_free(&three) ||
      !atomic_is_lock_free(&five) ||
      !atomic_is_lock_free(&twelve))
    return 1;
  if (atomic_is_lock_free(&seventeen) ||
      atomic_is_lock_free(&twenty_four))
    return 2;
  if (!atomic_is_lock_free(selected_scalar()) ||
      object_evaluations != 1)
    return 3;
  if (!_Generic(
          atomic_is_lock_free(&scalar),
          _Bool: 1,
          default: 0))
    return 4;
  return 0;
}
