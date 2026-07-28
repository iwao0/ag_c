/*
 * C11 NaN payloads consume a complete parenthesized n-char-sequence.
 * An unterminated payload leaves endptr immediately after "nan".
 */
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
  const char valid[] = "NaN(_A19)!";
  const char empty[] = "nan()?";
  const char unterminated[] = "nan(payload";
  const wchar_t wide_valid[] = L"NAN(_A19)!";
  const wchar_t wide_empty[] = L"nan()?";
  const wchar_t wide_unterminated[] = L"nan(payload";
  char *end = 0;
  wchar_t *wide_end = 0;

  if (!isnan(strtof_signature(valid, &end)) || *end != '!') return 1;
  if (!isnan(strtod_signature(empty, &end)) || *end != '?') return 2;
  if (!isnan(strtold_signature(unterminated, &end)) || *end != '(') return 3;

  if (!isnan(wcstof_signature(wide_valid, &wide_end)) ||
      *wide_end != L'!') {
    return 4;
  }
  if (!isnan(wcstod_signature(wide_empty, &wide_end)) ||
      *wide_end != L'?') {
    return 5;
  }
  if (!isnan(wcstold_signature(wide_unterminated, &wide_end)) ||
      *wide_end != L'(') {
    return 6;
  }
  return 0;
}
