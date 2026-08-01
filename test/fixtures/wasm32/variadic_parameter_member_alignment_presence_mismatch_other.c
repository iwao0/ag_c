// Paired with variadic_parameter_member_alignment_presence_mismatch_main.c.
#include <stdarg.h>

struct variadic_alignment_payload {
  int value;
};

_Static_assert(
    sizeof(struct variadic_alignment_payload) == sizeof(int),
    "aligned and plain definitions must have the same size");
_Static_assert(
    _Alignof(struct variadic_alignment_payload) == _Alignof(int),
    "aligned and plain definitions must have the same alignment");

int consume_variadic_parameter_alignment(
    int marker, struct variadic_alignment_payload value, ...) {
  va_list arguments;
  va_start(arguments, value);
  int extra = va_arg(arguments, int);
  va_end(arguments);
  return marker + value.value + extra;
}
