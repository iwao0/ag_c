/*
 * C11 strtof/wcstof range errors are determined after conversion to float,
 * not only while parsing the subject sequence as double.
 */
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <wchar.h>

static float (*strtof_signature)(const char *, char **) = strtof;
static double (*strtod_signature)(const char *, char **) = strtod;
static float (*wcstof_signature)(const wchar_t *, wchar_t **) = wcstof;
static double (*wcstod_signature)(const wchar_t *, wchar_t **) = wcstod;

int main(void) {
  const char narrow_overflow[] = "3.5e38x";
  const char narrow_subnormal[] = "1e-40x";
  const char narrow_zero[] = "-1e-100x";
  const wchar_t wide_overflow[] = L"-3.5e38x";
  const wchar_t wide_subnormal[] = L"1e-40x";
  const wchar_t wide_zero[] = L"-1e-100x";
  char *narrow_end = 0;
  wchar_t *wide_end = 0;
  double double_value;
  float float_value;

  errno = 0;
  double_value = strtod_signature(narrow_overflow, &narrow_end);
  if (double_value <= 3.0e38 || double_value >= 4.0e38 ||
      errno != 0 || *narrow_end != 'x') {
    return 1;
  }
  errno = 0;
  float_value = strtof_signature(narrow_overflow, &narrow_end);
  if (!isinf(float_value) || signbit(float_value) ||
      errno != ERANGE || *narrow_end != 'x') {
    return 2;
  }
  errno = 0;
  float_value = strtof_signature(narrow_subnormal, &narrow_end);
  if (float_value <= 0.0f || float_value >= 1.0e-30f ||
      errno != ERANGE || *narrow_end != 'x') {
    return 3;
  }
  errno = 0;
  float_value = strtof_signature(narrow_zero, &narrow_end);
  if (float_value != 0.0f || !signbit(float_value) ||
      errno != ERANGE || *narrow_end != 'x') {
    return 4;
  }

  errno = 0;
  double_value = wcstod_signature(wide_overflow, &wide_end);
  if (double_value >= -3.0e38 || double_value <= -4.0e38 ||
      errno != 0 || *wide_end != L'x') {
    return 5;
  }
  errno = 0;
  float_value = wcstof_signature(wide_overflow, &wide_end);
  if (!isinf(float_value) || !signbit(float_value) ||
      errno != ERANGE || *wide_end != L'x') {
    return 6;
  }
  errno = 0;
  float_value = wcstof_signature(wide_subnormal, &wide_end);
  if (float_value <= 0.0f || float_value >= 1.0e-30f ||
      errno != ERANGE || *wide_end != L'x') {
    return 7;
  }
  errno = 0;
  float_value = wcstof_signature(wide_zero, &wide_end);
  if (float_value != 0.0f || !signbit(float_value) ||
      errno != ERANGE || *wide_end != L'x') {
    return 8;
  }
  return 0;
}
