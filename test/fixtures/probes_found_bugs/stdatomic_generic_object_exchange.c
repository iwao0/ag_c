// stdatomic generic exchange/compare-exchange for floating, complex,
// aggregate, and promoted atomic-storage widths.
// Expected: exit=0
#include <complex.h>
#include <stdatomic.h>

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

struct pair {
  int x;
  int y;
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

static _Atomic(struct byte1) byte1_value =
    (struct byte1){1};
static _Atomic(struct bytes2) bytes2_value =
    (struct bytes2){2, 3};
static _Atomic(struct bytes3) bytes3_value =
    (struct bytes3){4, 5, 6};
static _Atomic(struct pair) pair_value =
    (struct pair){7, 8};
static _Atomic(struct bytes5) bytes5_value =
    (struct bytes5){9, 10, 11, 12, 13};
static _Atomic(struct words3) words3_value =
    (struct words3){14, 15, 16};
static _Atomic(union wide) wide_value =
    (union wide){.words = {0x1122334455667788ULL,
                           0x99aabbccddeeff00ULL}};
static _Atomic(float) float_value = 1.25f;
static _Atomic(double) double_value = 2.5;
static _Atomic(float complex) float_complex_value =
    CMPLXF(3.5f, -4.5f);
static _Atomic(double complex) double_complex_value =
    CMPLX(5.5, -6.5);

static int object_evaluations;
static int expected_evaluations;
static int value_evaluations;
static int order_evaluations;

static _Atomic(struct pair) *selected_pair(void) {
  object_evaluations++;
  return &pair_value;
}

static struct pair *selected_expected(struct pair *expected) {
  expected_evaluations++;
  return expected;
}

static struct pair next_pair(int x, int y) {
  value_evaluations++;
  return (struct pair){x, y};
}

static memory_order next_order(void) {
  order_evaluations++;
  return memory_order_seq_cst;
}

static int pair_is(struct pair value, int x, int y) {
  return value.x == x && value.y == y;
}

static int bytes5_is(
    struct bytes5 value, int a, int b, int c, int d, int e) {
  return value.a == a && value.b == b && value.c == c &&
         value.d == d && value.e == e;
}

static int words3_is(
    struct words3 value, unsigned int a,
    unsigned int b, unsigned int c) {
  return value.a == a && value.b == b && value.c == c;
}

int main(void) {
  struct byte1 old1 = atomic_exchange(
      &byte1_value, ((struct byte1){21}));
  struct bytes2 old2 = atomic_exchange(
      &bytes2_value, ((struct bytes2){22, 23}));
  struct bytes3 old3 = atomic_exchange(
      &bytes3_value, ((struct bytes3){24, 25, 26}));
  struct pair old8 = atomic_exchange(
      &pair_value, ((struct pair){27, 28}));
  struct bytes5 old5 = atomic_exchange(
      &bytes5_value, ((struct bytes5){29, 30, 31, 32, 33}));
  struct words3 old12 = atomic_exchange(
      &words3_value, ((struct words3){34, 35, 36}));
  union wide old16 = atomic_exchange(
      &wide_value,
      ((union wide){.words = {0x0123456789abcdefULL,
                              0xfedcba9876543210ULL}}));
  if (old1.a != 1 ||
      old2.a != 2 || old2.b != 3 ||
      old3.a != 4 || old3.b != 5 || old3.c != 6 ||
      !pair_is(old8, 7, 8) ||
      !bytes5_is(old5, 9, 10, 11, 12, 13) ||
      !words3_is(old12, 14, 15, 16) ||
      old16.words[0] != 0x1122334455667788ULL ||
      old16.words[1] != 0x99aabbccddeeff00ULL)
    return 1;

  float old_float = atomic_exchange(&float_value, 7);
  double old_double = atomic_exchange(&double_value, 8.5);
  float complex old_float_complex = atomic_exchange(
      &float_complex_value, 9.5f + 10.5f * I);
  double complex old_double_complex = atomic_exchange(
      &double_complex_value, 11.5 + 12.5 * I);
  if (old_float != 1.25f || old_double != 2.5 ||
      crealf(old_float_complex) != 3.5f ||
      cimagf(old_float_complex) != -4.5f ||
      creal(old_double_complex) != 5.5 ||
      cimag(old_double_complex) != -6.5)
    return 2;

  struct bytes3 expected3 = {24, 25, 26};
  if (!atomic_compare_exchange_strong(
          &bytes3_value, &expected3,
          ((struct bytes3){41, 42, 43})) ||
      expected3.a != 24 || expected3.b != 25 ||
      expected3.c != 26)
    return 3;
  expected3 = (struct bytes3){1, 2, 3};
  if (atomic_compare_exchange_strong(
          &bytes3_value, &expected3,
          ((struct bytes3){44, 45, 46})))
    return 40;
  if (expected3.a != 41)
    return 41;
  if (expected3.b != 42)
    return 42;
  if (expected3.c != 43)
    return 43;

  struct bytes5 expected5 = {29, 30, 31, 32, 33};
  if (!atomic_compare_exchange_weak_explicit(
          &bytes5_value, &expected5,
          ((struct bytes5){51, 52, 53, 54, 55}),
          memory_order_acq_rel, memory_order_acquire) ||
      !bytes5_is(expected5, 29, 30, 31, 32, 33))
    return 5;
  expected5 = (struct bytes5){1, 2, 3, 4, 5};
  if (atomic_compare_exchange_strong_explicit(
          &bytes5_value, &expected5,
          ((struct bytes5){56, 57, 58, 59, 60}),
          memory_order_seq_cst, memory_order_relaxed) ||
      !bytes5_is(expected5, 51, 52, 53, 54, 55))
    return 6;

  struct words3 expected12 = {34, 35, 36};
  if (!atomic_compare_exchange_strong(
          &words3_value, &expected12,
          ((struct words3){61, 62, 63})) ||
      !words3_is(expected12, 34, 35, 36))
    return 7;
  expected12 = (struct words3){1, 2, 3};
  if (atomic_compare_exchange_strong(
          &words3_value, &expected12,
          ((struct words3){64, 65, 66})) ||
      !words3_is(expected12, 61, 62, 63))
    return 8;

  union wide expected16 = {
      .words = {0x0123456789abcdefULL,
                0xfedcba9876543210ULL}};
  if (!atomic_compare_exchange_strong(
          &wide_value, &expected16,
          ((union wide){.words = {0x0f1e2d3c4b5a6978ULL,
                                  0x8877665544332211ULL}})) ||
      expected16.words[0] != 0x0123456789abcdefULL ||
      expected16.words[1] != 0xfedcba9876543210ULL)
    return 9;
  expected16 = (union wide){.words = {1, 2}};
  if (atomic_compare_exchange_weak(
          &wide_value, &expected16,
          ((union wide){.words = {3, 4}})) ||
      expected16.words[0] != 0x0f1e2d3c4b5a6978ULL ||
      expected16.words[1] != 0x8877665544332211ULL)
    return 10;

  float expected_float = 7.0f;
  double expected_double = 8.5;
  float complex expected_float_complex = 9.5f + 10.5f * I;
  double complex expected_double_complex = 11.5 + 12.5 * I;
  if (!atomic_compare_exchange_strong(
          &float_value, &expected_float, 13) ||
      !atomic_compare_exchange_strong(
          &double_value, &expected_double, 14) ||
      !atomic_compare_exchange_strong(
          &float_complex_value, &expected_float_complex,
          15.5f + 16.5f * I) ||
      !atomic_compare_exchange_strong(
          &double_complex_value, &expected_double_complex,
          17.5 + 18.5 * I))
    return 11;

  expected_float = -1.0f;
  expected_double_complex = -1.0 - 2.0 * I;
  if (atomic_compare_exchange_weak(
          &float_value, &expected_float, 19) ||
      expected_float != 13.0f ||
      atomic_compare_exchange_weak(
          &double_complex_value, &expected_double_complex,
          20.5 + 21.5 * I) ||
      creal(expected_double_complex) != 17.5 ||
      cimag(expected_double_complex) != 18.5)
    return 12;

  struct pair previous = atomic_exchange_explicit(
      selected_pair(), next_pair(71, 72), next_order());
  if (!pair_is(previous, 27, 28) ||
      object_evaluations != 1 ||
      value_evaluations != 1 ||
      order_evaluations != 1)
    return 13;
  struct pair expected_pair = {71, 72};
  if (!atomic_compare_exchange_strong_explicit(
          selected_pair(), selected_expected(&expected_pair),
          next_pair(73, 74), next_order(), next_order()) ||
      !pair_is(expected_pair, 71, 72) ||
      object_evaluations != 2 ||
      expected_evaluations != 1 ||
      value_evaluations != 2 ||
      order_evaluations != 3)
    return 14;

  _Atomic(struct pair) local_pair;
  atomic_init(&local_pair, ((struct pair){81, 82}));
  volatile _Atomic(struct pair) *volatile_pair = &local_pair;
  struct pair local_old = atomic_exchange(
      volatile_pair, ((struct pair){83, 84}));
  struct pair local_expected = {83, 84};
  if (!pair_is(local_old, 81, 82) ||
      !atomic_compare_exchange_strong(
          volatile_pair, &local_expected,
          ((struct pair){85, 86})))
    return 15;

  _Atomic(struct pair) type_pair;
  atomic_init(&type_pair, ((struct pair){91, 92}));
  if (!_Generic(
          atomic_exchange(
              &type_pair, ((struct pair){93, 94})),
          struct pair: 1, default: 0))
    return 16;
  return 0;
}
