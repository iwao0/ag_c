/*
 * C11 narrow and wide floating conversions: special values, hexadecimal
 * subject sequences, range errors, and end pointers.
 */
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <wchar.h>

static float (*strtof_signature)(const char *, char **) = strtof;
static double (*strtod_signature)(const char *, char **) = strtod;
static long double (*strtold_signature)(const char *, char **) = strtold;
static float (*wcstof_signature)(const wchar_t *, wchar_t **) = wcstof;
static double (*wcstod_signature)(const wchar_t *, wchar_t **) = wcstod;
static long double (*wcstold_signature)(const wchar_t *, wchar_t **) = wcstold;

int main(void) {
  const char narrow_infinity[] = " -INFINITY!";
  const char narrow_inf[] = "+iNf!";
  const char narrow_nan[] = "NaN(payload)?";
  const char narrow_hex[] = "0x1.8p+2!";
  const char narrow_hex_negative[] = "-0X1p-2?";
  const char narrow_overflow[] = "1e9999x";
  const char narrow_underflow[] = "-1e-9999x";
  const wchar_t wide_infinity[] = L" +infinity!";
  const wchar_t wide_inf[] = L"-Inf!";
  const wchar_t wide_nan[] = L"NAN(payload)?";
  const wchar_t wide_hex[] = L"0x1.8P+2!";
  const wchar_t wide_hex_negative[] = L"-0X1p-2?";
  const wchar_t wide_overflow[] = L"1e9999x";
  const wchar_t wide_underflow[] = L"-1e-9999x";
  char *narrow_end = 0;
  wchar_t *wide_end = 0;
  double double_value;

  double_value = strtod_signature(narrow_infinity, &narrow_end);
  if (!isinf(double_value) || !signbit(double_value) ||
      *narrow_end != '!') {
    return 1;
  }
  if (!isinf(strtof_signature(narrow_inf, &narrow_end)) ||
      signbit(strtof_signature(narrow_inf, 0)) || *narrow_end != '!') {
    return 2;
  }
  if (!isnan(strtof_signature(narrow_nan, &narrow_end)) ||
      *narrow_end != '?') {
    return 3;
  }
  if (strtod_signature(narrow_hex, &narrow_end) != 6.0 ||
      *narrow_end != '!') {
    return 4;
  }
  if (strtold_signature(narrow_hex_negative, &narrow_end) != -0.25L ||
      *narrow_end != '?') {
    return 5;
  }

  errno = 0;
  double_value = strtod_signature(narrow_overflow, &narrow_end);
  if (!isinf(double_value) || signbit(double_value) || errno != ERANGE ||
      *narrow_end != 'x') {
    return 6;
  }
  errno = 0;
  double_value = strtod_signature(narrow_underflow, &narrow_end);
  if (double_value != 0.0 || !signbit(double_value) || errno != ERANGE ||
      *narrow_end != 'x') {
    return 7;
  }

  double_value = wcstod_signature(wide_infinity, &wide_end);
  if (!isinf(double_value) || signbit(double_value) ||
      *wide_end != L'!') {
    return 8;
  }
  if (!isinf(wcstold_signature(wide_inf, &wide_end)) ||
      !signbit(wcstold_signature(wide_inf, 0)) || *wide_end != L'!') {
    return 9;
  }
  if (!isnan(wcstof_signature(wide_nan, &wide_end)) ||
      *wide_end != L'?') {
    return 10;
  }
  if (wcstod_signature(wide_hex, &wide_end) != 6.0 ||
      *wide_end != L'!') {
    return 11;
  }
  if (wcstold_signature(wide_hex_negative, &wide_end) != -0.25L ||
      *wide_end != L'?') {
    return 12;
  }

  errno = 0;
  double_value = wcstod_signature(wide_overflow, &wide_end);
  if (!isinf(double_value) || signbit(double_value) || errno != ERANGE ||
      *wide_end != L'x') {
    return 13;
  }
  errno = 0;
  double_value = wcstod_signature(wide_underflow, &wide_end);
  if (double_value != 0.0 || !signbit(double_value) || errno != ERANGE ||
      *wide_end != L'x') {
    return 14;
  }
  return 0;
}
