/*
 * C11 <inttypes.h> wide greatest-width conversion declarations, indirect
 * calls, end pointers, base handling, and 64-bit overflow behavior.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>

static intmax_t (*wcstoimax_signature)(const wchar_t *, wchar_t **, int) =
    wcstoimax;
static uintmax_t (*wcstoumax_signature)(const wchar_t *, wchar_t **, int) =
    wcstoumax;

int main(void) {
  const wchar_t signed_text[] = L" \t-0x2aZ";
  const wchar_t unsigned_text[] = L"18446744073709551615!";
  const wchar_t no_value[] = L"  +z";
  const wchar_t signed_overflow[] = L"9223372036854775808x";
  const wchar_t signed_underflow[] = L"-9223372036854775809x";
  const wchar_t unsigned_overflow[] = L"18446744073709551616x";
  wchar_t *end = 0;

  if (sizeof(intmax_t) != 8 || sizeof(uintmax_t) != 8) return 1;

  if (wcstoimax_signature(signed_text, &end, 0) != (intmax_t)-42 ||
      *end != L'Z') {
    return 2;
  }
  if (wcstoumax_signature(unsigned_text, &end, 10) != UINTMAX_MAX ||
      *end != L'!') {
    return 3;
  }
  if (wcstoimax_signature(L"z!", &end, 36) != (intmax_t)35 ||
      *end != L'!') {
    return 4;
  }
  if (wcstoumax_signature(L"-10!", &end, 10) != UINTMAX_MAX - 9 ||
      *end != L'!') {
    return 5;
  }
  if (wcstoimax_signature(no_value, &end, 10) != 0 || end != no_value) {
    return 6;
  }

  errno = 0;
  if (wcstoimax_signature(signed_overflow, &end, 10) != INTMAX_MAX ||
      errno != ERANGE || *end != L'x') {
    return 7;
  }
  errno = 0;
  if (wcstoimax_signature(signed_underflow, &end, 10) != INTMAX_MIN ||
      errno != ERANGE || *end != L'x') {
    return 8;
  }
  errno = 0;
  if (wcstoumax_signature(unsigned_overflow, &end, 10) != UINTMAX_MAX ||
      errno != ERANGE || *end != L'x') {
    return 9;
  }
  return 0;
}
