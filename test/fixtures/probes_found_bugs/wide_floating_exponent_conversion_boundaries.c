/*
 * C11 wide floating conversions must consume complete decimal exponents,
 * preserve the exponent marker when no exponent digit follows, and update
 * endptr relative to the original wide string.
 */
#include <wchar.h>

static float (*wcstof_signature)(const wchar_t *, wchar_t **) = wcstof;
static double (*wcstod_signature)(const wchar_t *, wchar_t **) = wcstod;
static long double (*wcstold_signature)(const wchar_t *, wchar_t **) = wcstold;

static int close_double(double actual, double expected, double tolerance) {
  double difference = actual - expected;
  if (difference < 0.0) difference = -difference;
  return difference <= tolerance;
}

int main(void) {
  const wchar_t negative[] = L" \t-1.25e2!";
  const wchar_t leading_fraction[] = L".5E+3?";
  const wchar_t negative_exponent[] = L"12e-2z";
  const wchar_t incomplete_exponent[] = L"7.5e+x";
  const wchar_t no_value[] = L"e10";
  wchar_t *end = 0;

  if (wcstof_signature(negative, &end) != -125.0f || *end != L'!') {
    return 1;
  }
  if (wcstod_signature(leading_fraction, &end) != 500.0 ||
      *end != L'?') {
    return 2;
  }
  if (!close_double((double)wcstold_signature(negative_exponent, &end),
                    0.12, 0.000000000000001) ||
      *end != L'z') {
    return 3;
  }
  if (wcstod_signature(incomplete_exponent, &end) != 7.5 ||
      *end != L'e') {
    return 4;
  }
  if (wcstod_signature(no_value, &end) != 0.0 || end != no_value) {
    return 5;
  }
  return 0;
}
