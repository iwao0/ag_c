#include <assert.h>

static int check_comparison_and_conditional(void) {
  double negative = -2.5;
  double positive = 1.25;

  assert(negative < positive);
  assert(negative <= negative);
  assert(positive > negative);
  assert(positive >= positive);
  assert(negative != positive);
  assert(!(negative == positive));

  double selected = positive > 0.0 ? negative : positive;
  assert(selected == -2.5);
  selected = negative > 0.0 ? negative : positive;
  assert(selected == 1.25);

  float narrow = 3.5f;
  assert(sizeof(1 ? narrow : positive) == sizeof(double));
  assert((1 ? narrow : positive) == 3.5);
  assert((0 ? narrow : positive) == 1.25);

  double negative_zero = -0.0;
  assert((negative_zero ? 1 : 2) == 2);
  assert(1.0 / (1 ? negative_zero : 0.0) < 0.0);
  return 0;
}

static int check_nan_and_infinity(void) {
  double zero = 0.0;
  double nan_value = zero / zero;
  double positive_infinity = 1.0 / zero;
  double negative_infinity = -1.0 / zero;

  assert(nan_value != nan_value);
  assert(!(nan_value == nan_value));
  assert(!(nan_value < 0.0));
  assert(!(nan_value <= 0.0));
  assert(!(nan_value > 0.0));
  assert(!(nan_value >= 0.0));
  assert((nan_value ? 7 : 9) == 7);

  assert(positive_infinity > 1.0e300);
  assert(negative_infinity < -1.0e300);
  assert(1.0 / positive_infinity == 0.0);
  assert(positive_infinity + negative_infinity !=
         positive_infinity + negative_infinity);
  return 0;
}

static int check_subnormal_values(void) {
  double tiny = 1.0;
  for (int i = 0; i < 1074; ++i) {
    tiny *= 0.5;
  }
  assert(tiny > 0.0);
  assert(tiny < 1.0e-300);
  assert(tiny * 2.0 > tiny);
  assert(tiny / 2.0 == 0.0);

  float narrow_tiny = 1.0f;
  for (int i = 0; i < 149; ++i) {
    narrow_tiny *= 0.5f;
  }
  assert(narrow_tiny > 0.0f);
  assert(narrow_tiny * 2.0f > narrow_tiny);
  assert(narrow_tiny / 2.0f == 0.0f);
  return 0;
}

int main(void) {
  assert(check_comparison_and_conditional() == 0);
  assert(check_nan_and_infinity() == 0);
  assert(check_subnormal_values() == 0);
  return 0;
}
