/*
 * A wide string stores each Unicode code point in one 4-byte wchar_t element
 * on the supported targets.  Pointer iteration must advance by elements, not
 * by the UTF-8 byte length of the source spelling.
 */

#include <wchar.h>

static wchar_t global_text[] =
    L"A\u4F60\u597D\u00A2\u4E16\u754C\u20AC\U0001F600Z";

_Static_assert(sizeof(wchar_t) == 4, "target wchar_t width");
_Static_assert(sizeof(global_text) / sizeof(global_text[0]) == 10,
               "nine code points and a terminator");

static int scan_wide(const wchar_t *text, unsigned long *sum,
                     const wchar_t **end) {
  int count = 0;
  unsigned long value = 0;

  while (*text != 0) {
    value += (unsigned long)*text;
    ++text;
    ++count;
  }
  *sum = value;
  *end = text;
  return count;
}

int main(void) {
  unsigned long global_sum = 0;
  const wchar_t *global_end = 0;
  unsigned long expected_global_sum =
      (unsigned long)L'A' + (unsigned long)L'\u4F60' +
      (unsigned long)L'\u597D' + (unsigned long)L'\u00A2' +
      (unsigned long)L'\u4E16' + (unsigned long)L'\u754C' +
      (unsigned long)L'\u20AC' + (unsigned long)L'\U0001F600' +
      (unsigned long)L'Z';
  wchar_t local_text[] = L"\u03B1\U0001F642\u03A9";
  const wchar_t *literal = L"\u3042\u3044";
  const wchar_t *literal_cursor = literal;
  wchar_t *compound =
      (wchar_t[]){L'x', L'\u20AC', L'y', 0};
  int literal_count = 0;

  if (global_text[0] != L'A') return 1;
  if (global_text[1] != 0x4F60 || global_text[2] != 0x597D) return 2;
  if (global_text[3] != 0x00A2) return 3;
  if (global_text[4] != 0x4E16 || global_text[5] != 0x754C) return 4;
  if (global_text[6] != 0x20AC) return 5;
  if (global_text[7] != 0x1F600) return 6;
  if (global_text[8] != L'Z' || global_text[9] != 0) return 7;

  if (scan_wide(global_text, &global_sum, &global_end) != 9) return 8;
  if (global_sum != expected_global_sum) return 9;
  if (global_end - global_text != 9 || *global_end != 0) return 10;

  if (local_text[0] != 0x03B1 || local_text[1] != 0x1F642 ||
      local_text[2] != 0x03A9 || local_text[3] != 0)
    return 11;
  if (&local_text[3] - &local_text[0] != 3) return 12;

  while (*literal_cursor != 0) {
    ++literal_cursor;
    ++literal_count;
  }
  if (literal_count != 2 || literal_cursor - literal != 2) return 13;
  if (literal[0] != 0x3042 || literal[1] != 0x3044 || literal[2] != 0)
    return 14;

  if (compound[0] != L'x' || compound[1] != 0x20AC ||
      compound[2] != L'y' || compound[3] != 0)
    return 15;
  compound += 2;
  if (*compound != L'y' || compound[-1] != 0x20AC) return 16;
  return 0;
}
