// stdatomic generic load/store for aggregate and complex object types.
// Expected: exit=0
#include <complex.h>
#include <stdatomic.h>

struct pair {
  int x;
  int y;
};

struct byte1 {
  unsigned char a;
};

struct bytes2 {
  unsigned char a;
  unsigned char b;
};

struct bytes3 {
  unsigned char a;
  unsigned char b;
  unsigned char c;
};

struct bytes5 {
  unsigned char a;
  unsigned char b;
  unsigned char c;
  unsigned char d;
  unsigned char e;
};

struct words3 {
  unsigned int a;
  unsigned int b;
  unsigned int c;
};

union wide {
  unsigned long long words[2];
  unsigned char bytes[16];
};

static _Atomic(struct pair) pair_value =
    (struct pair){1, 2};
static _Atomic(struct byte1) byte1_value =
    (struct byte1){31};
static _Atomic(struct bytes2) bytes2_value =
    (struct bytes2){32, 33};
static _Atomic(struct bytes3) bytes3_value =
    (struct bytes3){34, 35, 36};
static _Atomic(struct bytes5) bytes5_value =
    (struct bytes5){37, 38, 39, 40, 41};
static _Atomic(struct words3) words_value =
    (struct words3){3, 4, 5};
static _Atomic(union wide) wide_value =
    (union wide){.words = {0x1122334455667788ULL,
                           0x99aabbccddeeff00ULL}};
static _Atomic(float complex) float_value =
    CMPLXF(1.25f, -2.5f);
static _Atomic(double complex) double_value =
    CMPLX(3.5, -4.75);
static int object_evaluations;
static int value_evaluations;
static int order_evaluations;

static _Atomic(struct pair) *selected_pair(void) {
  object_evaluations++;
  return &pair_value;
}

static struct pair next_pair(void) {
  value_evaluations++;
  return (struct pair){21, 22};
}

static memory_order next_order(void) {
  order_evaluations++;
  return memory_order_relaxed;
}

static int is_pair(struct pair value, int x, int y) {
  return value.x == x && value.y == y;
}

static int is_words3(
    struct words3 value,
    unsigned int a, unsigned int b, unsigned int c) {
  return value.a == a && value.b == b && value.c == c;
}

static int is_float_complex(
    float complex value, float real, float imag) {
  return crealf(value) == real && cimagf(value) == imag;
}

static int is_double_complex(
    double complex value, double real, double imag) {
  return creal(value) == real && cimag(value) == imag;
}

int main(void) {
  struct pair pair = atomic_load(&pair_value);
  struct byte1 byte1 = atomic_load(&byte1_value);
  struct bytes2 bytes2 = atomic_load(&bytes2_value);
  struct bytes3 bytes3 = atomic_load(&bytes3_value);
  struct bytes5 bytes5 = atomic_load(&bytes5_value);
  struct words3 words = atomic_load(&words_value);
  union wide wide = atomic_load(&wide_value);
  float complex float_snapshot = atomic_load(&float_value);
  double complex double_snapshot = atomic_load(&double_value);
  if (!is_pair(pair, 1, 2) ||
      byte1.a != 31 ||
      bytes2.a != 32 || bytes2.b != 33 ||
      bytes3.a != 34 || bytes3.b != 35 || bytes3.c != 36 ||
      bytes5.a != 37 || bytes5.b != 38 || bytes5.c != 39 ||
      bytes5.d != 40 || bytes5.e != 41 ||
      !is_words3(words, 3, 4, 5) ||
      wide.words[0] != 0x1122334455667788ULL ||
      wide.words[1] != 0x99aabbccddeeff00ULL ||
      !is_float_complex(float_snapshot, 1.25f, -2.5f) ||
      !is_double_complex(double_snapshot, 3.5, -4.75))
    return 1;

  atomic_store(&pair_value, ((struct pair){6, 7}));
  atomic_store(&byte1_value, ((struct byte1){42}));
  atomic_store(&bytes2_value, ((struct bytes2){43, 44}));
  atomic_store(&bytes3_value, ((struct bytes3){45, 46, 47}));
  atomic_store(
      &bytes5_value, ((struct bytes5){48, 49, 50, 51, 52}));
  atomic_store(&words_value, ((struct words3){8, 9, 10}));
  atomic_store(
      &wide_value,
      ((union wide){.words = {0xfedcba9876543210ULL,
                              0x0123456789abcdefULL}}));
  atomic_store(&float_value, 11.25f - 12.5f * I);
  atomic_store(&double_value, 13.25 - 14.5 * I);
  pair = atomic_load(&pair_value);
  byte1 = atomic_load(&byte1_value);
  bytes2 = atomic_load(&bytes2_value);
  bytes3 = atomic_load(&bytes3_value);
  bytes5 = atomic_load(&bytes5_value);
  words = atomic_load(&words_value);
  wide = atomic_load(&wide_value);
  float_snapshot = atomic_load(&float_value);
  double_snapshot = atomic_load(&double_value);
  if (!is_pair(pair, 6, 7) ||
      byte1.a != 42 ||
      bytes2.a != 43 || bytes2.b != 44 ||
      bytes3.a != 45 || bytes3.b != 46 || bytes3.c != 47 ||
      bytes5.a != 48 || bytes5.b != 49 || bytes5.c != 50 ||
      bytes5.d != 51 || bytes5.e != 52 ||
      !is_words3(words, 8, 9, 10) ||
      wide.words[0] != 0xfedcba9876543210ULL ||
      wide.words[1] != 0x0123456789abcdefULL ||
      !is_float_complex(float_snapshot, 11.25f, -12.5f) ||
      !is_double_complex(double_snapshot, 13.25, -14.5))
    return 2;

  pair = atomic_load_explicit(
      selected_pair(), next_order());
  if (object_evaluations != 1 || order_evaluations != 1 ||
      !is_pair(pair, 6, 7))
    return 3;
  atomic_store_explicit(
      selected_pair(), next_pair(), next_order());
  if (object_evaluations != 2 || value_evaluations != 1 ||
      order_evaluations != 2 ||
      !is_pair(atomic_load(&pair_value), 21, 22))
    return 4;

  _Atomic(struct pair) local_pair;
  atomic_init(&local_pair, ((struct pair){23, 24}));
  volatile _Atomic(struct pair) *volatile_pointer = &local_pair;
  pair = atomic_load(volatile_pointer);
  atomic_store(volatile_pointer, ((struct pair){25, 26}));
  if (!is_pair(pair, 23, 24) ||
      !is_pair(atomic_load(volatile_pointer), 25, 26))
    return 5;

  if (!_Generic(atomic_load(&pair_value), struct pair: 1, default: 0) ||
      !_Generic(atomic_load(&words_value), struct words3: 1, default: 0) ||
      !_Generic(atomic_load(&double_value), double complex: 1, default: 0))
    return 6;
  return 0;
}
