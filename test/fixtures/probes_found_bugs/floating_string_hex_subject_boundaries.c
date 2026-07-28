/*
 * Hexadecimal floating conversion keeps the longest valid significand when
 * the optional-looking binary exponent text is incomplete, and reports the
 * end pointer at the first unconsumed character.
 */
#include <stdlib.h>
#include <wchar.h>

static double (*strtod_signature)(const char *, char **) = strtod;
static long double (*strtold_signature)(const char *, char **) = strtold;
static double (*wcstod_signature)(const wchar_t *, wchar_t **) = wcstod;
static long double (*wcstold_signature)(const wchar_t *, wchar_t **) = wcstold;

int main(void) {
  const char complete[] = "0x1.8p+2!";
  const char no_exponent[] = "0x1.8z";
  const char incomplete_exponent[] = "0x1p+x";
  const char no_digit[] = "0x.p1";
  const wchar_t wide_complete[] = L"0X1.8P+2!";
  const wchar_t wide_no_exponent[] = L"0X1.8z";
  const wchar_t wide_incomplete_exponent[] = L"0X1P+x";
  const wchar_t wide_no_digit[] = L"0X.P1";
  char *end = 0;
  wchar_t *wide_end = 0;

  if (strtod_signature(complete, &end) != 6.0 || *end != '!') return 1;
  if (strtold_signature(no_exponent, &end) != 1.5L || *end != 'z') return 2;
  if (strtod_signature(incomplete_exponent, &end) != 1.0 ||
      *end != 'p') {
    return 3;
  }
  if (strtod_signature(no_digit, &end) != 0.0 || *end != 'x') return 4;

  if (wcstod_signature(wide_complete, &wide_end) != 6.0 ||
      *wide_end != L'!') {
    return 5;
  }
  if (wcstold_signature(wide_no_exponent, &wide_end) != 1.5L ||
      *wide_end != L'z') {
    return 6;
  }
  if (wcstod_signature(wide_incomplete_exponent, &wide_end) != 1.0 ||
      *wide_end != L'P') {
    return 7;
  }
  if (wcstod_signature(wide_no_digit, &wide_end) != 0.0 ||
      *wide_end != L'X') {
    return 8;
  }
  return 0;
}
