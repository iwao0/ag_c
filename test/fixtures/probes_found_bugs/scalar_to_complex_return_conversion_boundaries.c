// C11 6.8.6.4 applies assignment conversion to the expression of a return
// statement.  Every real arithmetic type can therefore be returned from a
// function with a complex result type: the real part is converted to the
// component type and the imaginary part becomes zero.
typedef float _Complex float_complex;
typedef double _Complex double_complex;
typedef long double _Complex long_double_complex;

typedef float_complex (*float_returner)(void);
typedef double_complex (*double_returner)(void);
typedef long_double_complex (*long_double_returner)(void);

struct long_double_return_holder {
  long_double_returner call;
};

#define DEFINE_RETURNERS(tag, source_type, source_value)                    \
  static float_complex return_float_##tag(void) {                           \
    source_type value = (source_type)(source_value);                        \
    return value;                                                           \
  }                                                                         \
  static double_complex return_double_##tag(void) {                         \
    source_type value = (source_type)(source_value);                        \
    return value;                                                           \
  }                                                                         \
  static long_double_complex return_long_double_##tag(void) {               \
    source_type value = (source_type)(source_value);                        \
    return value;                                                           \
  }

DEFINE_RETURNERS(bool, _Bool, 1)
DEFINE_RETURNERS(signed_char, signed char, -2)
DEFINE_RETURNERS(unsigned_char, unsigned char, 3)
DEFINE_RETURNERS(signed_short, signed short, -4)
DEFINE_RETURNERS(unsigned_short, unsigned short, 5)
DEFINE_RETURNERS(signed_int, signed int, -6)
DEFINE_RETURNERS(unsigned_int, unsigned int, 7)
DEFINE_RETURNERS(signed_long, signed long, -8)
DEFINE_RETURNERS(unsigned_long, unsigned long, 9)
DEFINE_RETURNERS(signed_long_long, signed long long, -10)
DEFINE_RETURNERS(unsigned_long_long, unsigned long long, 11)
DEFINE_RETURNERS(float, float, 12.5f)
DEFINE_RETURNERS(double, double, -13.5)
DEFINE_RETURNERS(long_double, long double, 14.5L)

static int side_effect_count;

static int next_integer(void) {
  side_effect_count++;
  return -15;
}

static double_complex return_side_effect(void) {
  return next_integer();
}

static int float_is(float_complex value, float expected) {
  return __real__ value == expected && __imag__ value == 0.0f;
}

static int double_is(double_complex value, double expected) {
  return __real__ value == expected && __imag__ value == 0.0;
}

static int long_double_is(
    long_double_complex value, long double expected) {
  return __real__ value == expected && __imag__ value == 0.0L;
}

#define CHECK_RETURNERS(tag, expected)                                      \
  do {                                                                      \
    float_returner float_pointer = return_float_##tag;                      \
    double_returner double_pointer = return_double_##tag;                   \
    struct long_double_return_holder holder = {return_long_double_##tag};   \
    if (!float_is(return_float_##tag(), (float)(expected)) ||               \
        !float_is(float_pointer(), (float)(expected)) ||                    \
        !double_is(return_double_##tag(), (double)(expected)) ||            \
        !double_is(double_pointer(), (double)(expected)) ||                 \
        !long_double_is(return_long_double_##tag(),                         \
                        (long double)(expected)) ||                         \
        !long_double_is(holder.call(), (long double)(expected)))            \
      return __LINE__;                                                      \
  } while (0)

int main(void) {
  CHECK_RETURNERS(bool, 1);
  CHECK_RETURNERS(signed_char, -2);
  CHECK_RETURNERS(unsigned_char, 3);
  CHECK_RETURNERS(signed_short, -4);
  CHECK_RETURNERS(unsigned_short, 5);
  CHECK_RETURNERS(signed_int, -6);
  CHECK_RETURNERS(unsigned_int, 7);
  CHECK_RETURNERS(signed_long, -8);
  CHECK_RETURNERS(unsigned_long, 9);
  CHECK_RETURNERS(signed_long_long, -10);
  CHECK_RETURNERS(unsigned_long_long, 11);
  CHECK_RETURNERS(float, 12.5f);
  CHECK_RETURNERS(double, -13.5);
  CHECK_RETURNERS(long_double, 14.5L);

  side_effect_count = 0;
  if (!double_is(return_side_effect(), -15.0) ||
      side_effect_count != 1)
    return 100;

  double_returner pointer = return_side_effect;
  side_effect_count = 0;
  if (!double_is(pointer(), -15.0) ||
      side_effect_count != 1)
    return 101;

  return 0;
}
