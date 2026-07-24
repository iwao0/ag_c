#include <assert.h>

static int ternary_true = 1.0 ? 3 : 4;
static int ternary_false = 0.0 ? 3 : 4;
static int negative_zero_false = -0.0 ? 3 : 4;
static int logical_or = 1.0 || (1 / 0);
static int logical_and = 0.0 && (1 / 0);
static int greater = 2.0 > 1.0;
static int equal = 2.0 == 2.0;
static int mixed_less = 1 < 1.5;
static double selected_true = 1.0 ? 2.5 : 3.5;
static double selected_false = 0.0 ? 2.5 : 3.5;
static double selected_by_integer = 1 ? 4.5 : 5.5;
static double comparison_as_double = 2.0 > 1.0;
static double logical_as_double = 0.0 || 2.0;
static double integer_ternary_as_double = 1 ? 12 : 13;
static _Bool fractional_bool = (_Bool)-0.5;
static _Bool negative_zero_bool = (_Bool)-0.0;
static double fractional_bool_as_double = (double)(_Bool)-0.5;
static double high_unsigned_as_double = (double)(1ULL << 63);
static double max_unsigned_as_double =
    (double)18446744073709551615ULL;
static unsigned long long high_fraction_as_unsigned =
    (unsigned long long)9223372036854777856.0;
static unsigned long long implicit_high_fraction_as_unsigned =
    18446744073709549568.0;
static _Bool floating_bool_values[] = {-0.5, -0.0};
static int condition_values[] = {
  1.0 ? 6 : 7,
  0.0 || 1.0,
  0.0 && (1 / 0)
};
static struct {
  int comparison;
  double selected;
  _Bool fractional_bool;
  unsigned long long high_unsigned;
} condition_record = {
  3.0 >= 2.0,
  0.0 ? 8.5 : 9.5,
  -0.25,
  9223372036854777856.0
};

int main(void) {
  static int local_truth = 0.0 || 2.0;
  static double local_selected = 1.0 ? 10.5 : 11.5;
  static _Bool local_fractional_bool = -0.125;
  static unsigned long long local_high_unsigned =
      9223372036854777856.0;

  assert(ternary_true == 3);
  assert(ternary_false == 4);
  assert(negative_zero_false == 4);
  assert(logical_or == 1);
  assert(logical_and == 0);
  assert(greater == 1);
  assert(equal == 1);
  assert(mixed_less == 1);
  assert(selected_true == 2.5);
  assert(selected_false == 3.5);
  assert(selected_by_integer == 4.5);
  assert(comparison_as_double == 1.0);
  assert(logical_as_double == 1.0);
  assert(integer_ternary_as_double == 12.0);
  assert(fractional_bool == 1);
  assert(negative_zero_bool == 0);
  assert(fractional_bool_as_double == 1.0);
  assert(high_unsigned_as_double == 9223372036854775808.0);
  assert(max_unsigned_as_double == 18446744073709551616.0);
  assert(high_fraction_as_unsigned == 9223372036854777856ULL);
  assert(implicit_high_fraction_as_unsigned ==
         18446744073709549568ULL);
  assert(floating_bool_values[0] == 1);
  assert(floating_bool_values[1] == 0);
  assert(condition_values[0] == 6);
  assert(condition_values[1] == 1);
  assert(condition_values[2] == 0);
  assert(condition_record.comparison == 1);
  assert(condition_record.selected == 9.5);
  assert(condition_record.fractional_bool == 1);
  assert(condition_record.high_unsigned == 9223372036854777856ULL);
  assert(local_truth == 1);
  assert(local_selected == 10.5);
  assert(local_fractional_bool == 1);
  assert(local_high_unsigned == 9223372036854777856ULL);
  return 0;
}
