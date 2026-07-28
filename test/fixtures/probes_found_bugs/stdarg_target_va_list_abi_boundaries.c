/*
 * Preserve va_list's target ABI identity as well as by-value and pointer
 * forwarding through the standard formatted I/O interfaces.
 */
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

#ifdef __wasm32__
_Static_assert(_Generic((va_list)0, long: 1, default: 0),
               "Wasm va_list is the 64-bit runtime cursor");
_Static_assert(sizeof(va_list) == 8, "Wasm va_list width");
_Static_assert(_Alignof(va_list) == 8, "Wasm va_list alignment");
#else
_Static_assert(_Generic((va_list)0, char *: 1, default: 0),
               "Apple arm64 va_list is a character pointer");
_Static_assert(sizeof(va_list) == sizeof(void *), "native va_list width");
_Static_assert(_Alignof(va_list) == _Alignof(void *),
               "native va_list alignment");
#endif

static int (*vsnprintf_signature)(
    char *, size_t, const char *, va_list) = vsnprintf;
static int (*vsscanf_signature)(
    const char *, const char *, va_list) = vsscanf;
static int (*vswprintf_signature)(
    wchar_t *, size_t, const wchar_t *, va_list) = vswprintf;
static int (*vswscanf_signature)(
    const wchar_t *, const wchar_t *, va_list) = vswscanf;

#ifndef __clang__
static int read_pair(va_list *arguments) {
  int first = va_arg(*arguments, int);
  int second = va_arg(*arguments, int);
  return first * 10 + second;
}

static int verify_forwarding(const char *format, ...) {
  va_list primary;
  va_list copy;
  int primary_value;
  int copy_value;

  va_start(primary, format);
  va_copy(copy, primary);
  primary_value = read_pair(&primary);
  copy_value = read_pair(&copy);
  va_end(copy);
  va_end(primary);
  return primary_value == 42 && copy_value == 42;
}

static int format_narrow(char *buffer, size_t size, const char *format, ...) {
  va_list arguments;
  int result;
  va_start(arguments, format);
  result = vsnprintf_signature(buffer, size, format, arguments);
  va_end(arguments);
  return result;
}

static int scan_narrow(const char *input, const char *format, ...) {
  va_list arguments;
  int result;
  va_start(arguments, format);
  result = vsscanf_signature(input, format, arguments);
  va_end(arguments);
  return result;
}

static int format_wide(
    wchar_t *buffer, size_t size, const wchar_t *format, ...) {
  va_list arguments;
  int result;
  va_start(arguments, format);
  result = vswprintf_signature(buffer, size, format, arguments);
  va_end(arguments);
  return result;
}

static int scan_wide(
    const wchar_t *input, const wchar_t *format, ...) {
  va_list arguments;
  int result;
  va_start(arguments, format);
  result = vswscanf_signature(input, format, arguments);
  va_end(arguments);
  return result;
}
#endif

int main(void) {
  assert(vsnprintf_signature != NULL);
  assert(vsscanf_signature != NULL);
  assert(vswprintf_signature != NULL);
  assert(vswscanf_signature != NULL);
#ifndef __clang__
  {
    char narrow[16];
    wchar_t wide[16];
    int narrow_value = 0;
    int wide_value = 0;

    assert(verify_forwarding("%d%d", 4, 2));
    assert(format_narrow(narrow, sizeof(narrow), "%d:%d", 4, 2) == 3);
    assert(narrow[0] == '4' && narrow[1] == ':' && narrow[2] == '2');
    assert(scan_narrow("42", "%d", &narrow_value) == 1);
    assert(narrow_value == 42);
    assert(format_wide(
               wide, sizeof(wide) / sizeof(wide[0]), L"%d:%d", 4, 2) == 3);
    assert(wide[0] == L'4' && wide[1] == L':' && wide[2] == L'2');
    assert(scan_wide(L"42", L"%d", &wide_value) == 1);
    assert(wide_value == 42);
  }
#endif
  return 0;
}
