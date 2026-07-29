/*
 * Conditional operands undergo the usual value conversions before their
 * common result type is selected.  Atomic lvalues therefore produce
 * non-atomic values, and only the selected operand is evaluated.
 */
#include <assert.h>
#include <complex.h>

#define TYPE_IS(expression, type) \
  _Generic((expression), type: 1, default: 0)

struct Pair {
  int first;
  long second;
};

static int values[4] = {3, 5, 7, 9};
static _Atomic int atomic_integers[2] = {11, 13};
static _Atomic(unsigned long long) atomic_wide[2] = {
    0x0123456789abcdefULL,
    0xfedcba9876543210ULL,
};
static _Atomic(float) atomic_float = 1.25f;
static _Atomic(double) atomic_double = 2.5;
static _Atomic(float complex) atomic_float_complex = CMPLXF(3.25f, -4.5f);
static _Atomic(double complex) atomic_double_complex = CMPLX(5.25, -6.5);
static _Atomic(int *) atomic_pointers[2] = {values, values + 2};
static _Atomic(struct Pair) atomic_pair_left = (struct Pair){17, 19};
static _Atomic(struct Pair) atomic_pair_right = (struct Pair){23, 29};

static int left_selections;
static int right_selections;

static void selected(int side) {
  if (side == 0)
    left_selections++;
  else
    right_selections++;
}

static _Atomic int *select_integer(int side) {
  selected(side);
  return &atomic_integers[side];
}

static _Atomic(unsigned long long) *select_wide(int side) {
  selected(side);
  return &atomic_wide[side];
}

static _Atomic(float) *select_float(void) {
  selected(0);
  return &atomic_float;
}

static _Atomic(double) *select_double(void) {
  selected(1);
  return &atomic_double;
}

static _Atomic(float complex) *select_float_complex(void) {
  selected(0);
  return &atomic_float_complex;
}

static _Atomic(double complex) *select_double_complex(void) {
  selected(1);
  return &atomic_double_complex;
}

static _Atomic(int *) *select_pointer(int side) {
  selected(side);
  return &atomic_pointers[side];
}

static _Atomic(struct Pair) *select_pair(int side) {
  selected(side);
  if (side == 0)
    return &atomic_pair_left;
  return &atomic_pair_right;
}

_Static_assert(TYPE_IS(1 ? atomic_integers[0] : atomic_integers[1], int),
               "conditional converts atomic integer lvalues");
_Static_assert(
    TYPE_IS(1 ? atomic_wide[0] : atomic_wide[1], unsigned long long),
    "conditional converts wide atomic integer lvalues");
_Static_assert(TYPE_IS(1 ? atomic_float : atomic_double, double),
               "conditional applies arithmetic conversion after atomic load");
_Static_assert(
    TYPE_IS(1 ? atomic_float_complex : atomic_double_complex, double complex),
    "conditional applies complex conversion after atomic load");
_Static_assert(TYPE_IS(1 ? atomic_pointers[0] : atomic_pointers[1], int *),
               "conditional converts atomic pointer lvalues");
_Static_assert(TYPE_IS(1 ? atomic_pair_left : atomic_pair_right, struct Pair),
               "conditional converts atomic aggregate lvalues");

static void check_selected_values(int choose_left) {
  int integer =
      choose_left ? *select_integer(0) : *select_integer(1);
  unsigned long long wide =
      choose_left ? *select_wide(0) : *select_wide(1);
  double real =
      choose_left ? *select_float() : *select_double();
  double complex complex_value =
      choose_left ? *select_float_complex() : *select_double_complex();
  int *pointer =
      choose_left ? *select_pointer(0) : *select_pointer(1);
  struct Pair pair =
      choose_left ? *select_pair(0) : *select_pair(1);

  if (choose_left) {
    assert(integer == 11);
    assert(wide == 0x0123456789abcdefULL);
    assert(real == 1.25);
    assert(creal(complex_value) == 3.25);
    assert(cimag(complex_value) == -4.5);
    assert(pointer == values);
    assert(pair.first == 17);
    assert(pair.second == 19);
  } else {
    assert(integer == 13);
    assert(wide == 0xfedcba9876543210ULL);
    assert(real == 2.5);
    assert(creal(complex_value) == 5.25);
    assert(cimag(complex_value) == -6.5);
    assert(pointer == values + 2);
    assert(pair.first == 23);
    assert(pair.second == 29);
  }
}

int main(void) {
  check_selected_values(1);
  assert(left_selections == 6);
  assert(right_selections == 0);

  check_selected_values(0);
  assert(left_selections == 6);
  assert(right_selections == 6);
  return 0;
}
